#include "btb/snapshot.hpp"

#include <algorithm>
#include <cstdlib>

#include "btb/json.hpp"
#include "btb/news.hpp"

namespace btb::snapshot {

void write(const MarketSim& sim, Frame& f) {
  const Exchange& ex = sim.exchange();
  const OrderBook& book = ex.book();
  const Account& a = ex.account(kPlayer);
  const ContractSpec& spec = ex.spec();
  const Price mark = ex.mark();
  const Cents equity = ex.equity(kPlayer);

  f.fill(0);
  f[Version] = kVersion;
  f[TimeUs] = std::max<SimTime>(0, std::min(sim.now(), sim.round_us()));
  f[RoundUs] = sim.round_us();
  f[PhaseSlot] = static_cast<std::int64_t>(sim.phase());
  f[Finished] = sim.finished();
  f[BestBid] = book.has_bid() ? book.best_bid() : 0;
  f[BestAsk] = book.has_ask() ? book.best_ask() : 0;
  f[LastTrade] = ex.has_last_trade() ? ex.last_trade() : 0;
  f[Mark] = mark;
  f[Position] = a.position;
  f[OpenCost] = a.open_cost;
  f[Realized] = a.realized(spec.tick_value);
  f[Unrealized] = a.unrealized(mark, spec.tick_value);
  f[Equity] = equity;
  f[InitialMargin] = ex.risk().initial_requirement(a);
  f[MaintMargin] = ex.risk().maintenance_requirement(a);
  f[TicksToLiq] = ex.risk().ticks_to_liquidation(a, equity);
  f[OpenBuy] = a.open_buy;
  f[OpenSell] = a.open_sell;
  f[Liquidating] = a.liquidating;
  f[Fees] = a.fees;
  f[Fills] = a.fills;
  f[TotalPnl] = ex.total_pnl(kPlayer);
  f[MaxDrawdown] = sim.max_drawdown();
  f[StartingCash] = spec.starting_cash;
  f[TickValue] = spec.tick_value;
  f[TicksPerPoint] = spec.ticks_per_point;
  f[MaxPosition] = spec.max_position;
  f[PriceBand] = spec.price_band;
  f[Liquidations] = a.liquidations;

  LevelView lv[kLevels];
  const std::uint32_t nb = book.depth(Side::Buy, lv, kLevels);
  for (std::uint32_t i = 0; i < nb; ++i) {
    f[kBidsOffset + i * 3] = lv[i].price;
    f[kBidsOffset + i * 3 + 1] = lv[i].qty;
    f[kBidsOffset + i * 3 + 2] = lv[i].orders;
  }
  const std::uint32_t na = book.depth(Side::Sell, lv, kLevels);
  for (std::uint32_t i = 0; i < na; ++i) {
    f[kAsksOffset + i * 3] = lv[i].price;
    f[kAsksOffset + i * 3 + 1] = lv[i].qty;
    f[kAsksOffset + i * 3 + 2] = lv[i].orders;
  }
  f[BidCount] = nb;
  f[AskCount] = na;

  int n = 0;
  for (const OrderId id : ex.tracked_orders()) {
    if (n == kMaxOrders) break;
    const Order* o = book.find(id);
    if (!o) continue;
    const int base = kOrdersOffset + n * 4;
    f[base] = static_cast<std::int64_t>(o->id);
    f[base + 1] = static_cast<std::int64_t>(o->side);
    f[base + 2] = o->price;
    f[base + 3] = o->qty;
    ++n;
  }
  f[OrderCount] = n;
}

namespace {

void kv(std::string& out, const char* key, std::int64_t v, bool comma = true) {
  json::append_string(out, key);
  out += ':';
  out += std::to_string(v);
  if (comma) out += ',';
}

void kv(std::string& out, const char* key, const char* v, bool comma = true) {
  json::append_string(out, key);
  out += ':';
  json::append_string(out, v);
  if (comma) out += ',';
}

}  // namespace

std::string summary_json(const MarketSim& sim) {
  const RoundSummary r = sim.summary();
  std::string s = "{";
  kv(s, "seed", static_cast<std::int64_t>(sim.seed()));
  kv(s, "difficulty", to_string(sim.config().difficulty));
  kv(s, "scenario", to_string(sim.config().scenario));
  kv(s, "pnl", r.pnl);
  kv(s, "returnBp", r.return_bp);
  kv(s, "maxDrawdown", r.max_drawdown);
  kv(s, "fills", r.fills);
  kv(s, "closes", r.closes);
  kv(s, "wins", r.wins);
  kv(s, "botsBeaten", r.bots_beaten);
  kv(s, "botCount", r.bot_count);
  kv(s, "liquidations", r.liquidations);
  kv(s, "goalMet", r.goal_met ? 1 : 0);
  kv(s, "settlementPrice", r.settlement_price);
  if (sim.finished()) kv(s, "fairValue", r.final_fair_value);

  s += "\"bots\":[";
  bool first = true;
  for (const auto& b : sim.bots()) {
    if (!first) s += ',';
    first = false;
    s += '{';
    kv(s, "kind", to_string(b->kind()));
    kv(s, "pnl", sim.exchange().total_pnl(b->id()), false);
    s += '}';
  }
  s += "],\"equity\":[";
  first = true;
  for (const Cents e : sim.equity_curve()) {
    if (!first) s += ',';
    first = false;
    s += std::to_string(e);
  }
  s += ']';
  // The hidden value is the answer key: only reveal it once trading is over.
  if (sim.finished()) {
    s += ",\"fairValues\":[";
    first = true;
    for (const Price p : sim.fair_value_curve()) {
      if (!first) s += ',';
      first = false;
      s += std::to_string(p);
    }
    s += ']';
  }
  s += '}';
  return s;
}

std::string actions_json(const std::vector<PlayerAction>& log) {
  std::string s = "[";
  for (std::size_t i = 0; i < log.size(); ++i) {
    const PlayerAction& a = log[i];
    if (i) s += ',';
    const std::int64_t f[kActionFields] = {static_cast<std::int64_t>(a.tick), static_cast<std::int64_t>(a.kind),
                                           static_cast<std::int64_t>(a.side), static_cast<std::int64_t>(a.type),
                                           a.price, a.qty, static_cast<std::int64_t>(a.id)};
    for (int k = 0; k < kActionFields; ++k) {
      if (k) s += ',';
      s += std::to_string(f[k]);
    }
  }
  s += ']';
  return s;
}

std::optional<std::vector<PlayerAction>> parse_actions(std::string_view text, std::string* error) {
  auto fail = [&](const char* msg) -> std::optional<std::vector<PlayerAction>> {
    if (error) *error = msg;
    return std::nullopt;
  };
  const auto flat = json::parse_int_array(text, error);
  if (!flat) return std::nullopt;
  if (flat->size() % kActionFields) return fail("action log length is not a multiple of 7");
  std::vector<PlayerAction> log;
  log.reserve(flat->size() / kActionFields);
  for (std::size_t i = 0; i < flat->size(); i += kActionFields) {
    const std::int64_t* f = flat->data() + i;
    if (f[0] < 0 || (!log.empty() && static_cast<std::uint64_t>(f[0]) < log.back().tick))
      return fail("action ticks must be non-negative and in order");
    if (f[1] < 0 || f[1] > static_cast<std::int64_t>(ActionKind::Flatten)) return fail("bad action kind");
    if (f[2] < 0 || f[2] > static_cast<std::int64_t>(Side::Sell)) return fail("bad side");
    if (f[3] < 0 || f[3] > static_cast<std::int64_t>(OrderType::IOC)) return fail("bad order type");
    if (f[5] < 0 || f[5] > 1'000'000 || f[6] < 0) return fail("bad quantity or order id");
    log.push_back({static_cast<std::uint64_t>(f[0]), static_cast<ActionKind>(f[1]), static_cast<Side>(f[2]),
                   static_cast<OrderType>(f[3]), static_cast<Price>(f[4]), static_cast<Qty>(f[5]),
                   static_cast<OrderId>(f[6])});
  }
  return log;
}

std::string catalog_json() {
  std::string s = "{\"headlines\":[";
  std::size_t n = 0;
  const Headline* h = headline_catalog(&n);
  for (std::size_t i = 0; i < n; ++i) {
    if (i) s += ',';
    s += '{';
    kv(s, "text", h[i].text);
    kv(s, "sentiment", h[i].sentiment, false);
    s += '}';
  }
  s += "],\"scenarios\":[";
  const ScenarioInfo* sc = scenarios(&n);
  for (std::size_t i = 0; i < n; ++i) {
    if (i) s += ',';
    s += '{';
    kv(s, "key", sc[i].key);
    kv(s, "title", sc[i].title);
    kv(s, "goal", sc[i].goal);
    kv(s, "seed", static_cast<std::int64_t>(sc[i].seed));
    kv(s, "difficulty", to_string(sc[i].difficulty), false);
    s += '}';
  }
  s += "],\"rejectReasons\":[";
  for (int i = 0; i <= static_cast<int>(RejectReason::NoLiquidity); ++i) {
    if (i) s += ',';
    json::append_string(s, to_string(static_cast<RejectReason>(i)));
  }
  s += "],\"cancelReasons\":[";
  for (int i = 0; i <= static_cast<int>(CancelReason::Expiry); ++i) {
    if (i) s += ',';
    json::append_string(s, to_string(static_cast<CancelReason>(i)));
  }
  s += "]}";
  return s;
}

std::uint64_t self_test(std::uint64_t seed, const GameConfig& cfg) {
  MarketSim sim(seed, cfg);
  Rng rng(seed ^ 0x5E1F7E57ull);
  while (!sim.finished()) {
    if (rng.chance(1, 40)) {
      const Side side = rng.chance(1, 2) ? Side::Buy : Side::Sell;
      switch (rng.below(5)) {
        case 0:
        case 1: sim.player_submit(side, OrderType::Market, 0, static_cast<Qty>(rng.range(1, 3))); break;
        case 2: {
          const auto r = sim.player_submit(side, OrderType::Limit, sim.exchange().mark() + rng.range(-4, 4), 1);
          if (r.resting && rng.chance(1, 2)) sim.player_cancel(r.id);
          break;
        }
        case 3: sim.player_flatten(); break;
        default: sim.player_cancel_all(); break;
      }
    }
    sim.step(static_cast<std::uint32_t>(rng.range(5, 40)));  // uneven frame times on purpose
  }
  return sim.exchange().events().hash();
}

}  // namespace btb::snapshot
