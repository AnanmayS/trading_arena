#include "btb/market_sim.hpp"

#include <algorithm>
#include <chrono>

namespace btb {

namespace {

// Independent RNG streams derived from the one round seed, so adding a bot or
// changing one component never perturbs the others.
std::uint64_t stream(std::uint64_t seed, std::uint64_t index) {
  std::uint64_t z = seed + 0x9E3779B97F4A7C15ull * (index + 1);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

enum Stream : std::uint64_t { kFairValue = 0, kNews = 1, kSchedule = 2, kBots = 16 };

}  // namespace

// Routes a bot's orders to the exchange under its participant id.
class MarketSim::Sink final : public OrderSink {
 public:
  Sink(Exchange& ex, ParticipantId id) : ex_(ex), id_(id) {}
  SubmitResult submit(Side side, OrderType type, Price price, Qty qty) override {
    return ex_.submit(id_, {id_, side, type, price, qty});
  }
  bool cancel(OrderId id) override { return ex_.cancel(id_, id); }
  void cancel_all() override { ex_.cancel_all(id_); }

 private:
  Exchange& ex_;
  ParticipantId id_;
};

MarketSim::MarketSim(std::uint64_t seed, const GameConfig& cfg)
    : seed_(seed),
      cfg_(cfg),
      tick_us_(static_cast<SimTime>(cfg.tick_ms) * 1000),
      round_us_(static_cast<SimTime>(cfg.round_seconds) * 1'000'000),
      ex_(cfg.contract, cfg.market.initial_price, cfg.events),
      fv_(cfg.market.initial_price, cfg.market.vol_fp, cfg.tick_ms, stream(seed, kFairValue)),
      view_(ex_, history_, public_news_),
      sched_rng_(stream(seed, kSchedule)) {
  ex_.add_participant(true);  // the player is always participant 0

  Rng news_rng(stream(seed, kNews));
  news_ = make_news_schedule(cfg_, news_rng);

  std::uint64_t n = kBots;
  auto add = [&](auto make) {
    const ParticipantId id = ex_.add_participant(false);
    bots_.push_back(make(id, stream(seed, n++)));
    sinks_.push_back(std::make_unique<Sink>(ex_, id));
  };
  const Price p0 = cfg.market.initial_price;
  for (int i = 0; i < cfg.makers.count; ++i)
    add([&](ParticipantId id, std::uint64_t s) { return std::make_unique<MarketMaker>(id, s, cfg.makers, p0); });
  for (int i = 0; i < cfg.informed.count; ++i)
    add([&](ParticipantId id, std::uint64_t s) { return std::make_unique<InformedTrader>(id, s, cfg.informed, fv_); });
  for (int i = 0; i < cfg.momentum.count; ++i)
    add([&](ParticipantId id, std::uint64_t s) { return std::make_unique<MomentumTrader>(id, s, cfg.momentum); });
  for (int i = 0; i < cfg.mean_rev.count; ++i)
    add([&](ParticipantId id, std::uint64_t s) { return std::make_unique<MeanReversionTrader>(id, s, cfg.mean_rev); });
  for (int i = 0; i < cfg.noise.count; ++i)
    add([&](ParticipantId id, std::uint64_t s) { return std::make_unique<NoiseTrader>(id, s, cfg.noise); });

  order_.resize(bots_.size());
  for (std::uint32_t i = 0; i < order_.size(); ++i) order_[i] = i;

  history_.push(p0);
  peak_equity_ = ex_.equity(kPlayer);
  equity_curve_.reserve(cfg.round_seconds + 2);
  equity_curve_.push_back(peak_equity_);

  // Pre-open: let the bots build a book for one simulated second so the
  // player's first click at t=0 meets real liquidity.
  now_ = -kPreOpenUs;
  next_sample_ = now_ + PriceHistory::kIntervalUs;
  while (now_ < 0) tick();
  ticks_ = 0;
  engine_ns_ = 0;
  fv_curve_.reserve(cfg.round_seconds + 1);
  fv_curve_.push_back(fv_.value());
}

MarketSim::MarketSim(std::uint64_t seed, const GameConfig& cfg, std::vector<PlayerAction> log)
    : MarketSim(seed, cfg) {
  replay_log_ = std::move(log);
  replaying_ = true;
}

MarketSim::~MarketSim() = default;

template <class Fn>
auto MarketSim::timed(Fn&& fn) {
  const auto t0 = std::chrono::steady_clock::now();
  struct Guard {
    std::chrono::steady_clock::time_point t0;
    std::uint64_t& acc;
    ~Guard() {
      acc += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    }
  } guard{t0, engine_ns_};
  return fn();
}

void MarketSim::step(std::uint32_t ms) {
  if (finished_) return;
  // Cap catch-up after a long pause (backgrounded tab) to two seconds.
  acc_us_ = std::min<SimTime>(acc_us_ + static_cast<SimTime>(ms) * 1000, 2'000'000);
  while (acc_us_ >= tick_us_ && !finished_) {
    acc_us_ -= tick_us_;
    advance();
  }
}

void MarketSim::run_ticks(std::uint64_t n) {
  for (std::uint64_t i = 0; i < n && !finished_; ++i) advance();
}

void MarketSim::run_to_end() {
  while (!finished_) advance();
}

void MarketSim::advance() {
  while (replay_next_ < replay_log_.size() && replay_log_[replay_next_].tick <= ticks_)
    apply(replay_log_[replay_next_++]);
  tick();
  // Actions logged after expiry are rejected, but still part of the record.
  if (finished_)
    while (replay_next_ < replay_log_.size()) apply(replay_log_[replay_next_++]);
}

std::int64_t MarketSim::regime_fp() const {
  // Calm for the first 30%, normal through 80%, then volatility ramps up
  // linearly into expiry.
  const std::int64_t pct_x100 = now_ * 10000 / round_us_;  // 0..10000
  const MarketParams& mp = cfg_.market;
  if (pct_x100 < 3000) return mp.open_vol_mult_fp;
  if (pct_x100 < 8000) return kFp;
  return kFp + (mp.close_vol_mult_fp - kFp) * (pct_x100 - 8000) / 2000;
}

void MarketSim::tick() {
  timed([&] {
    ++ticks_;
    now_ += tick_us_;
    ex_.set_time(now_);

    // 1. News: publish headlines, then apply any jumps that are due.
    while (next_news_ < news_.size() && news_[next_news_].release_us <= now_) {
      const NewsItem& n = news_[next_news_++];
      ex_.publish_news(n.headline, n.sentiment);
      public_news_ = {now_, n.sentiment, n.jump, n.jump_us};
      if (n.jump != 0) pending_jumps_.push_back(n);
    }
    std::erase_if(pending_jumps_, [&](const NewsItem& n) {
      if (n.jump_us > now_) return false;
      fv_.jump(n.jump);
      return true;
    });

    // 2. Hidden value moves.
    fv_.step(regime_fp());

    // 3. Bots act in a freshly shuffled order each tick so no archetype gets
    // a permanent speed advantage.
    for (std::uint32_t i = static_cast<std::uint32_t>(order_.size()); i > 1; --i)
      std::swap(order_[i - 1], order_[sched_rng_.below(i)]);
    for (const std::uint32_t i : order_) bots_[i]->on_tick(view_, *sinks_[i]);
    ex_.check_stops();
    ex_.check_margins();

    // 4. Bookkeeping: price history, equity, per-second stats.
    while (now_ >= next_sample_) {
      history_.push(ex_.mark());
      next_sample_ += PriceHistory::kIntervalUs;
    }
    const Cents eq = ex_.equity(kPlayer);
    peak_equity_ = std::max(peak_equity_, eq);
    max_dd_ = std::max(max_dd_, peak_equity_ - eq);
    if (now_ >= next_second_) {
      equity_curve_.push_back(eq);
      fv_curve_.push_back(fv_.value());
      events_last_second_ = ex_.events().total() - events_at_second_;
      events_at_second_ = ex_.events().total();
      next_second_ += 1'000'000;
    }

    // 5. Expiry.
    const SimTime window = static_cast<SimTime>(cfg_.settlement_window_seconds) * 1'000'000;
    if (!settle_window_open_ && now_ >= round_us_ - window) {
      ex_.begin_settlement_window(now_);
      settle_window_open_ = true;
    }
    if (now_ >= round_us_) {
      ex_.settle();
      equity_curve_.push_back(ex_.equity(kPlayer));
      finished_ = true;
    }
  });
}

Phase MarketSim::phase() const {
  if (finished_) return Phase::Settled;
  if (settle_window_open_) return Phase::Settlement;
  const std::int64_t pct = now_ * 100 / round_us_;
  if (pct < 30) return Phase::Open;
  if (pct < 80) return Phase::Midday;
  return Phase::Close;
}

// ------------------------------------------------------------ player

SubmitResult MarketSim::player_submit(Side side, OrderType type, Price price, Qty qty) {
  actions_.push_back({ticks_, ActionKind::Submit, side, type, price, qty, kNoOrder});
  return timed([&] { return ex_.submit(kPlayer, {kPlayer, side, type, price, qty}); });
}

bool MarketSim::player_cancel(OrderId id) {
  actions_.push_back({ticks_, ActionKind::Cancel, Side::Buy, OrderType::Limit, 0, 0, id});
  return timed([&] { return ex_.cancel(kPlayer, id); });
}

std::uint32_t MarketSim::player_cancel_all() {
  actions_.push_back({ticks_, ActionKind::CancelAll, Side::Buy, OrderType::Limit, 0, 0, kNoOrder});
  return timed([&] { return ex_.cancel_all(kPlayer); });
}

SubmitResult MarketSim::player_flatten() {
  actions_.push_back({ticks_, ActionKind::Flatten, Side::Buy, OrderType::Limit, 0, 0, kNoOrder});
  return timed([&] { return ex_.flatten(kPlayer); });
}

void MarketSim::apply(const PlayerAction& a) {
  switch (a.kind) {
    case ActionKind::Submit: player_submit(a.side, a.type, a.price, a.qty); break;
    case ActionKind::Cancel: player_cancel(a.id); break;
    case ActionKind::CancelAll: player_cancel_all(); break;
    case ActionKind::Flatten: player_flatten(); break;
  }
}

std::uint64_t MarketSim::replay(std::uint64_t seed, const GameConfig& cfg, const std::vector<PlayerAction>& log) {
  MarketSim sim(seed, cfg, log);
  sim.run_to_end();
  return sim.exchange().events().hash();
}

// ------------------------------------------------------------ results

EngineStats MarketSim::stats() const {
  EngineStats s;
  const ExchangeStats& es = ex_.stats();
  s.orders = es.orders + es.cancels;
  s.trades = es.trades;
  s.events = ex_.events().total();
  s.events_per_sec = events_last_second_;
  s.avg_ns_per_order = s.orders ? engine_ns_ / s.orders : 0;
  s.ticks = ticks_;
  return s;
}

bool MarketSim::goal_met(Cents pnl, std::uint32_t liquidations) const {
  switch (cfg_.scenario) {
    case Scenario::FlashCrash: return liquidations == 0;
    case Scenario::NewsDay: return pnl >= 100'000;
    case Scenario::None:
    case Scenario::ShortSqueeze:
    case Scenario::ThinLiquidity: return pnl > 0;
  }
  return false;
}

RoundSummary MarketSim::summary() const {
  RoundSummary r;
  const Account& a = ex_.account(kPlayer);
  r.pnl = ex_.total_pnl(kPlayer);
  r.return_bp = r.pnl * 10000 / cfg_.contract.starting_cash;
  r.max_drawdown = max_dd_;
  r.fills = a.fills;
  r.closes = a.closes;
  r.wins = a.winning_closes;
  r.liquidations = a.liquidations;
  r.bot_count = static_cast<std::uint32_t>(bots_.size());
  for (const auto& b : bots_)
    if (ex_.total_pnl(b->id()) < r.pnl) ++r.bots_beaten;
  r.goal_met = goal_met(r.pnl, a.liquidations);
  r.settlement_price = ex_.closed() ? ex_.mark() : 0;
  r.final_fair_value = fv_.value();
  return r;
}

}  // namespace btb
