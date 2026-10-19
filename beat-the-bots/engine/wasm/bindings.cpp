// embind layer: exposes one Engine class to JavaScript. No game logic lives
// here; it only forwards commands to MarketSim and hands out pointers to the
// preallocated frame and event buffers that TypeScript reads directly from
// WebAssembly memory.
#include <emscripten/bind.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "btb/market_sim.hpp"
#include "btb/snapshot.hpp"

namespace {

using namespace btb;

constexpr std::uint32_t kMaxEventsPerDrain = 8192;

// Doubles because JS numbers are doubles; counts stay far below 2^53.
struct StatsView {
  double orders, trades, events, eventsPerSec, avgNsPerOrder, ticks;
};

class Engine {
 public:
  Engine(double seed, const std::string& config_json) : events_(kMaxEventsPerDrain) {
    auto cfg = parse_config(config_json, &error_);
    if (!cfg) cfg = GameConfig{};
    sim_ = std::make_unique<MarketSim>(static_cast<std::uint64_t>(seed), *cfg);
    refresh();
  }

  // Replay of a finished round: `actions_json` is a log from actionLogJson().
  Engine(double seed, const std::string& config_json, const std::string& actions_json)
      : events_(kMaxEventsPerDrain) {
    auto cfg = parse_config(config_json, &error_);
    if (!cfg) cfg = GameConfig{};
    std::string log_error;
    auto log = snapshot::parse_actions(actions_json, &log_error);
    if (!log) {
      if (error_.empty()) error_ = "action log: " + log_error;
      log.emplace();
    }
    sim_ = std::make_unique<MarketSim>(static_cast<std::uint64_t>(seed), *cfg, std::move(*log));
    refresh();
  }

  std::string configError() const { return error_; }

  void step(int ms) {
    if (ms > 0) sim_->step(static_cast<std::uint32_t>(ms));
  }

  // Returns the new order id, or 0 if rejected (the reason arrives as a
  // Reject event).
  double submit(int side, int type, int price_ticks, int qty) {
    const SubmitResult r =
        sim_->player_submit(static_cast<Side>(side), static_cast<OrderType>(type), price_ticks, qty);
    return static_cast<double>(r.id);
  }
  bool cancel(double id) { return sim_->player_cancel(static_cast<OrderId>(id)); }
  int cancelAll() { return static_cast<int>(sim_->player_cancel_all()); }
  void flatten() { sim_->player_flatten(); }

  // Rewrites the frame snapshot and drains pending events into the export
  // buffer. Returns the number of events drained.
  int refresh() {
    snapshot::write(*sim_, frame_);
    event_count_ = sim_->exchange().events().drain(events_.data(), kMaxEventsPerDrain);
    return static_cast<int>(event_count_);
  }

  std::uintptr_t framePtr() const { return reinterpret_cast<std::uintptr_t>(frame_.data()); }
  int frameSize() const { return snapshot::kFrameSize; }
  std::uintptr_t eventsPtr() const { return reinterpret_cast<std::uintptr_t>(events_.data()); }
  int eventCount() const { return static_cast<int>(event_count_); }
  bool finished() const { return sim_->finished(); }
  bool replaying() const { return sim_->replaying(); }

  StatsView getStats() const {
    const EngineStats s = sim_->stats();
    return {static_cast<double>(s.orders),         static_cast<double>(s.trades),
            static_cast<double>(s.events),         static_cast<double>(s.events_per_sec),
            static_cast<double>(s.avg_ns_per_order), static_cast<double>(s.ticks)};
  }

  std::string summaryJson() const { return snapshot::summary_json(*sim_); }
  std::string actionLogJson() const { return snapshot::actions_json(sim_->action_log()); }

  static std::string catalog() { return snapshot::catalog_json(); }

  // Hex string because a 64-bit hash does not fit in a JS number.
  static std::string selfTest(double seed, const std::string& config_json) {
    auto cfg = parse_config(config_json);
    if (!cfg) return "config-error";
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx",
                  static_cast<unsigned long long>(snapshot::self_test(static_cast<std::uint64_t>(seed), *cfg)));
    return buf;
  }

 private:
  std::unique_ptr<MarketSim> sim_;
  std::string error_;
  snapshot::Frame frame_{};
  std::vector<Event> events_;
  std::uint32_t event_count_ = 0;
};

}  // namespace

EMSCRIPTEN_BINDINGS(btb) {
  using namespace emscripten;
  value_object<StatsView>("Stats")
      .field("orders", &StatsView::orders)
      .field("trades", &StatsView::trades)
      .field("events", &StatsView::events)
      .field("eventsPerSec", &StatsView::eventsPerSec)
      .field("avgNsPerOrder", &StatsView::avgNsPerOrder)
      .field("ticks", &StatsView::ticks);

  class_<Engine>("Engine")
      .constructor<double, std::string>()
      .constructor<double, std::string, std::string>()
      .function("configError", &Engine::configError)
      .function("step", &Engine::step)
      .function("submit", &Engine::submit)
      .function("cancel", &Engine::cancel)
      .function("cancelAll", &Engine::cancelAll)
      .function("flatten", &Engine::flatten)
      .function("refresh", &Engine::refresh)
      .function("framePtr", &Engine::framePtr)
      .function("frameSize", &Engine::frameSize)
      .function("eventsPtr", &Engine::eventsPtr)
      .function("eventCount", &Engine::eventCount)
      .function("finished", &Engine::finished)
      .function("replaying", &Engine::replaying)
      .function("getStats", &Engine::getStats)
      .function("summaryJson", &Engine::summaryJson)
      .function("actionLogJson", &Engine::actionLogJson)
      .class_function("catalog", &Engine::catalog)
      .class_function("selfTest", &Engine::selfTest);
}
