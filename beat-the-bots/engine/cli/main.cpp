// Headless round runner: simulates a full round without a UI and prints market
// statistics. Used to tune the bots and as a smoke test in CI.
//
//   btb_cli [--seed N] [--difficulty easy|normal|hard] [--scenario KEY]
//           [--seconds N] [--player none|random] [--headlines]
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "btb/market_sim.hpp"
#include "btb/news.hpp"

using namespace btb;

namespace {

std::string dollars(Cents c) {
  char buf[64];
  const char* sign = c < 0 ? "-" : "";
  const long long a = std::llabs(c);
  std::snprintf(buf, sizeof buf, "%s$%lld.%02lld", sign, a / 100, a % 100);
  return buf;
}

std::string points(Price ticks) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld.%02lld", static_cast<long long>(ticks / 4),
                static_cast<long long>((ticks % 4) * 25));
  return buf;
}

[[noreturn]] void usage() {
  std::fprintf(stderr,
               "usage: btb_cli [--seed N] [--difficulty easy|normal|hard] [--scenario KEY]\n"
               "               [--seconds N] [--player none|random] [--headlines]\n");
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  std::uint64_t seed = 42;
  std::string difficulty = "normal", scenario = "none", player = "none";
  int seconds = 300;
  bool show_headlines = false;
  for (int i = 1; i < argc; ++i) {
    auto next = [&]() -> const char* {
      if (i + 1 >= argc) usage();
      return argv[++i];
    };
    if (!std::strcmp(argv[i], "--seed")) seed = std::strtoull(next(), nullptr, 10);
    else if (!std::strcmp(argv[i], "--difficulty")) difficulty = next();
    else if (!std::strcmp(argv[i], "--scenario")) scenario = next();
    else if (!std::strcmp(argv[i], "--seconds")) seconds = std::atoi(next());
    else if (!std::strcmp(argv[i], "--player")) player = next();
    else if (!std::strcmp(argv[i], "--headlines")) show_headlines = true;
    else usage();
  }

  std::string error;
  const std::string json = "{\"difficulty\":\"" + difficulty + "\",\"scenario\":\"" + scenario +
                           "\",\"round_seconds\":" + std::to_string(seconds) + "}";
  const auto cfg = parse_config(json, &error);
  if (!cfg) {
    std::fprintf(stderr, "config error: %s\n", error.c_str());
    return 2;
  }

  MarketSim sim(seed, *cfg);
  Rng player_rng(seed ^ 0xABCDEF);
  Price hi = cfg->market.initial_price, lo = hi;
  std::int64_t track_err = 0, samples = 0;
  bool zero_sum = true;

  const auto t0 = std::chrono::steady_clock::now();
  while (!sim.finished()) {
    if (player == "random" && player_rng.chance(1, 60)) {
      const auto r = player_rng.below(10);
      if (r < 6)
        sim.player_submit(player_rng.chance(1, 2) ? Side::Buy : Side::Sell, OrderType::Market, 0,
                          static_cast<Qty>(player_rng.range(1, 3)));
      else if (r < 9)
        sim.player_submit(player_rng.chance(1, 2) ? Side::Buy : Side::Sell, OrderType::Limit,
                          sim.exchange().mark() + player_rng.range(-6, 6), 1);
      else
        sim.player_flatten();
    }
    sim.run_ticks(1);
    const Price m = sim.exchange().mark();
    hi = std::max(hi, m);
    lo = std::min(lo, m);
    track_err += std::llabs(m - sim.fair_value());
    ++samples;
    zero_sum = zero_sum && sim.exchange().zero_sum_holds();
  }
  const double wall_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

  const Exchange& ex = sim.exchange();
  const EngineStats st = sim.stats();
  const RoundSummary sum = sim.summary();

  std::printf("Beat the Bots: headless round\n");
  std::printf("  seed %" PRIu64 "  difficulty %s  scenario %s  %us  tick %ums\n", seed, to_string(cfg->difficulty),
              to_string(cfg->scenario), cfg->round_seconds, cfg->tick_ms);
  std::printf("  participants: 1 player + %zu bots\n\n", sim.bots().size());

  std::printf("price      open %s  high %s  low %s  settle %s  (fair value %s)\n",
              points(cfg->market.initial_price).c_str(), points(hi).c_str(), points(lo).c_str(),
              points(sum.settlement_price).c_str(), points(sum.final_fair_value).c_str());
  std::printf("tracking   mean |mark - fair value| = %.2f ticks\n", static_cast<double>(track_err) / samples);
  std::printf("activity   %" PRIu64 " trades, %" PRIu64 " contracts, %" PRIu64 " orders+cancels, %" PRIu64
              " events\n",
              ex.stats().trades, ex.stats().contracts, st.orders, st.events);

  if (show_headlines) {
    std::size_t n_headlines = 0;
    const Headline* catalog = headline_catalog(&n_headlines);
    std::printf("\nheadlines\n");
    for (const NewsItem& n : sim.news())
      std::printf("  %6.1fs  %+d  jump %+4lld ticks @ %6.1fs  %s\n", n.release_us / 1e6, n.sentiment,
                  static_cast<long long>(n.jump), n.jump_us / 1e6, catalog[n.headline].text);
  }

  std::map<std::string, std::pair<Cents, int>> by_kind;
  for (const auto& b : sim.bots()) {
    auto& slot = by_kind[to_string(b->kind())];
    slot.first += ex.total_pnl(b->id());
    slot.second += 1;
  }
  std::printf("\nP&L by bot type (net of fees)\n");
  for (const auto& [kind, v] : by_kind)
    std::printf("  %-15s x%-3d %12s\n", kind.c_str(), v.second, dollars(v.first).c_str());
  std::printf("  %-15s      %12s\n", "fees collected", dollars(ex.total_fees()).c_str());
  std::printf("  %-15s      %12s  (%u fills, %u liquidations, beat %u/%u bots)\n", "player",
              dollars(sum.pnl).c_str(), sum.fills, sum.liquidations, sum.bots_beaten, sum.bot_count);

  std::printf("\nzero-sum   %s\n", zero_sum ? "OK after every tick" : "VIOLATED");
  std::printf("hash       0x%016" PRIx64 "\n", ex.events().hash());
  std::printf("engine     %.1f ms wall for %us simulated (%.0fx real time), %.0f orders/s\n", wall_ms,
              cfg->round_seconds, cfg->round_seconds * 1000.0 / wall_ms, st.orders / (wall_ms / 1000.0));
  return zero_sum ? 0 : 1;
}
