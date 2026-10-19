// The whole game round: exchange, hidden fair value, news schedule and bots,
// advanced in fixed simulation ticks.
//
// Determinism contract: a round is a pure function of (seed, config, player
// action log). The UI calls step(ms) with wall-clock deltas, but the engine
// only ever advances in whole ticks, and each player action is stamped with
// the number of ticks completed before it. Replaying the log against the same
// seed reproduces the exact event stream (see replay()).
#pragma once

#include <memory>
#include <vector>

#include "btb/bots.hpp"
#include "btb/config.hpp"
#include "btb/exchange.hpp"
#include "btb/fair_value.hpp"
#include "btb/market_view.hpp"
#include "btb/news.hpp"
#include "btb/rng.hpp"

namespace btb {

enum class ActionKind : std::uint8_t { Submit = 0, Cancel = 1, CancelAll = 2, Flatten = 3 };

struct PlayerAction {
  std::uint64_t tick = 0;  // ticks completed before the action
  ActionKind kind = ActionKind::Submit;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;
  Qty qty = 0;
  OrderId id = kNoOrder;
};

enum class Phase : std::uint8_t { Open = 0, Midday = 1, Close = 2, Settlement = 3, Settled = 4 };

struct EngineStats {
  std::uint64_t orders = 0;
  std::uint64_t trades = 0;
  std::uint64_t events = 0;
  std::uint64_t events_per_sec = 0;  // over the last simulated second
  std::uint64_t avg_ns_per_order = 0;  // wall time inside the engine per order
  std::uint64_t ticks = 0;
};

struct RoundSummary {
  Cents pnl = 0;
  std::int64_t return_bp = 0;  // basis points of starting cash
  Cents max_drawdown = 0;
  std::uint32_t fills = 0;
  std::uint32_t closes = 0;
  std::uint32_t wins = 0;
  std::uint32_t bots_beaten = 0;
  std::uint32_t bot_count = 0;
  std::uint32_t liquidations = 0;
  bool goal_met = false;
  Price settlement_price = 0;
  Price final_fair_value = 0;
};

class MarketSim {
 public:
  MarketSim(std::uint64_t seed, const GameConfig& cfg);
  ~MarketSim();
  MarketSim(const MarketSim&) = delete;
  MarketSim& operator=(const MarketSim&) = delete;

  // Advances by wall-clock milliseconds; runs as many whole ticks as fit.
  void step(std::uint32_t ms);
  // Advances exactly `n` ticks (tests, replay, headless runs).
  void run_ticks(std::uint64_t n);
  void run_to_end();

  // ------------------------------------------------------------ player
  SubmitResult player_submit(Side side, OrderType type, Price price, Qty qty);
  bool player_cancel(OrderId id);
  std::uint32_t player_cancel_all();
  SubmitResult player_flatten();
  void apply(const PlayerAction& a);
  const std::vector<PlayerAction>& action_log() const { return actions_; }

  // Re-runs a round from its seed, config and action log; returns the final
  // event-stream hash.
  static std::uint64_t replay(std::uint64_t seed, const GameConfig& cfg, const std::vector<PlayerAction>& log);

  // ------------------------------------------------------------ state
  bool finished() const { return finished_; }
  SimTime now() const { return now_; }
  SimTime round_us() const { return round_us_; }
  std::uint64_t ticks() const { return ticks_; }
  Phase phase() const;
  std::uint64_t seed() const { return seed_; }
  const GameConfig& config() const { return cfg_; }

  Exchange& exchange() { return ex_; }
  const Exchange& exchange() const { return ex_; }
  const PriceHistory& history() const { return history_; }
  const std::vector<NewsItem>& news() const { return news_; }
  const std::vector<std::unique_ptr<Bot>>& bots() const { return bots_; }
  // The hidden value; the UI only reveals it after the round.
  Price fair_value() const { return fv_.value(); }

  EngineStats stats() const;
  // Player equity sampled once per simulated second.
  const std::vector<Cents>& equity_curve() const { return equity_curve_; }
  Cents max_drawdown() const { return max_dd_; }
  // Valid once finished().
  RoundSummary summary() const;

 private:
  static constexpr SimTime kPreOpenUs = 1'000'000;
  class Sink;
  void tick();
  std::int64_t regime_fp() const;
  bool goal_met(Cents pnl, std::uint32_t liquidations) const;
  template <class Fn>
  auto timed(Fn&& fn);

  std::uint64_t seed_;
  GameConfig cfg_;
  SimTime tick_us_;
  SimTime round_us_;

  Exchange ex_;
  FairValue fv_;
  PriceHistory history_;
  PublicNews public_news_;
  MarketView view_;
  std::vector<NewsItem> news_;
  std::size_t next_news_ = 0;
  std::vector<NewsItem> pending_jumps_;

  std::vector<std::unique_ptr<Bot>> bots_;
  std::vector<std::unique_ptr<Sink>> sinks_;
  std::vector<std::uint32_t> order_;
  Rng sched_rng_;

  SimTime now_ = 0;
  SimTime acc_us_ = 0;
  SimTime next_sample_ = 0;
  SimTime next_second_ = 1'000'000;
  std::uint64_t ticks_ = 0;
  bool settle_window_open_ = false;
  bool finished_ = false;

  std::vector<PlayerAction> actions_;
  std::vector<Cents> equity_curve_;
  Cents peak_equity_ = 0;
  Cents max_dd_ = 0;

  std::uint64_t engine_ns_ = 0;
  std::uint64_t events_last_second_ = 0;
  std::uint64_t events_at_second_ = 0;
};

}  // namespace btb
