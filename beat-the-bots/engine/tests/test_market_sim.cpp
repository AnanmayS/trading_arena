#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

#include "btb/config.hpp"
#include "btb/fair_value.hpp"
#include "btb/market_sim.hpp"
#include "btb/news.hpp"
#include "btb/snapshot.hpp"

using namespace btb;

namespace {

GameConfig short_round(Difficulty d = Difficulty::Normal, Scenario s = Scenario::None, std::uint32_t secs = 60) {
  GameConfig c = make_config(d, s);
  c.round_seconds = secs;
  return c;
}

// A scripted, deterministic "player" that trades every so often.
void play_randomly(MarketSim& sim, std::uint64_t seed) {
  Rng rng(seed);
  while (!sim.finished()) {
    if (rng.chance(1, 40)) {
      const auto r = rng.below(10);
      const Side side = rng.chance(1, 2) ? Side::Buy : Side::Sell;
      if (r < 5) {
        sim.player_submit(side, OrderType::Market, 0, static_cast<Qty>(rng.range(1, 3)));
      } else if (r < 8) {
        auto res = sim.player_submit(side, OrderType::Limit, sim.exchange().mark() + rng.range(-5, 5), 1);
        if (res.resting && rng.chance(1, 2)) sim.player_cancel(res.id);
      } else if (r == 8) {
        sim.player_flatten();
      } else {
        sim.player_cancel_all();
      }
    }
    sim.run_ticks(1);
  }
}

}  // namespace

TEST_CASE("isqrt is exact") {
  for (std::uint64_t x : {0ull, 1ull, 2ull, 3ull, 4ull, 15ull, 16ull, 17ull, 1ull << 40, (1ull << 62) + 12345}) {
    const std::uint64_t r = isqrt(x);
    CHECK(r * r <= x);
    CHECK((r + 1) * (r + 1) > x);
  }
}

TEST_CASE("fair value process is deterministic and its volatility stays bounded") {
  FairValue a(20000, 2 * kFp, 25, 99), b(20000, 2 * kFp, 25, 99);
  for (int i = 0; i < 20000; ++i) {
    a.step(kFp);
    b.step(kFp);
    REQUIRE(a.raw_fp() == b.raw_fp());
    REQUIRE(a.vol_fp() >= 2 * kFp / 3);
    REQUIRE(a.vol_fp() <= 2 * kFp * 3);
  }
  a.jump(-40);
  CHECK(a.value() == b.value() - 40);
}

TEST_CASE("news schedule: sorted, inside the round, leads match difficulty") {
  for (Difficulty d : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard}) {
    const GameConfig cfg = make_config(d);
    Rng rng(5);
    const auto items = make_news_schedule(cfg, rng);
    REQUIRE(items.size() == static_cast<std::size_t>(cfg.news.headlines) + 1);  // + mid-round shock
    bool has_shock = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const NewsItem& n = items[i];
      if (i) CHECK(items[i - 1].release_us <= n.release_us);
      CHECK(n.release_us > 0);
      CHECK(n.jump_us < static_cast<SimTime>(cfg.round_seconds) * 1'000'000);
      if (n.sentiment != 0) {
        CHECK(n.jump_us - n.release_us >= cfg.news.lead_min_us);
        CHECK(n.jump_us - n.release_us <= cfg.news.lead_max_us);
        CHECK((n.jump > 0) == (n.sentiment > 0));
      } else {
        CHECK(n.jump == 0);
      }
      has_shock = has_shock || std::abs(n.sentiment) == 3;
    }
    CHECK(has_shock);
  }
}

TEST_CASE("config parsing: presets, overrides and errors") {
  auto c = parse_config(R"({"difficulty":"hard","scenario":"thin_liquidity","round_seconds":120,"tick_ms":20})");
  REQUIRE(c);
  CHECK(c->difficulty == Difficulty::Hard);
  CHECK(c->scenario == Scenario::ThinLiquidity);
  CHECK(c->round_seconds == 120);
  CHECK(c->tick_ms == 20);
  CHECK(c->makers.count == 2);

  CHECK(parse_config("")->difficulty == Difficulty::Normal);
  CHECK(parse_config("{}"));
  std::string err;
  CHECK_FALSE(parse_config(R"({"difficulty":"insane"})", &err));
  CHECK(err.find("difficulty") != std::string::npos);
  CHECK_FALSE(parse_config(R"({"round_seconds":1.5})", &err));
  CHECK_FALSE(parse_config(R"({"round_seconds":5})", &err));
  CHECK_FALSE(parse_config(R"({"a":{"b":1}})", &err));
  CHECK_FALSE(parse_config(R"({"a":1)", &err));
}

TEST_CASE("same seed, same market; different seed, different market") {
  const GameConfig cfg = short_round();
  MarketSim a(1234, cfg), b(1234, cfg), c(1235, cfg);
  a.run_to_end();
  b.run_to_end();
  c.run_to_end();
  CHECK(a.exchange().events().hash() == b.exchange().events().hash());
  CHECK(a.exchange().stats().trades == b.exchange().stats().trades);
  CHECK(a.fair_value() == b.fair_value());
  CHECK(a.exchange().events().hash() != c.exchange().events().hash());
}

TEST_CASE("step(ms) granularity does not change the market") {
  // 60 fps frames vs 1 big step per second: identical outcome.
  const GameConfig cfg = short_round(Difficulty::Normal, Scenario::None, 30);
  MarketSim a(77, cfg), b(77, cfg);
  while (!a.finished()) a.step(16);
  while (!b.finished()) b.step(1000);
  CHECK(a.exchange().events().hash() == b.exchange().events().hash());
}

TEST_CASE("replaying the player's action log reproduces the round exactly") {
  for (std::uint64_t seed : {3ull, 4ull, 5ull}) {
    const GameConfig cfg = short_round(Difficulty::Hard);
    MarketSim live(seed, cfg);
    play_randomly(live, seed * 7);
    REQUIRE(live.action_log().size() > 20);
    CHECK(MarketSim::replay(seed, cfg, live.action_log()) == live.exchange().events().hash());
  }
}

TEST_CASE("a replay sim stepped like the UI matches the live round, P&L included") {
  const GameConfig cfg = short_round(Difficulty::Normal, Scenario::None, 30);
  MarketSim live(11, cfg);
  play_randomly(live, 99);
  REQUIRE(live.action_log().size() > 10);

  // Round-trip the log through its JSON form, as the browser does.
  const auto log = snapshot::parse_actions(snapshot::actions_json(live.action_log()));
  REQUIRE(log);
  REQUIRE(log->size() == live.action_log().size());

  MarketSim replay(11, cfg, *log);
  CHECK(replay.replaying());
  Rng frames(5);
  while (!replay.finished()) replay.step(static_cast<std::uint32_t>(frames.range(16, 400)));  // 4x-8x speed frames
  CHECK(replay.exchange().events().hash() == live.exchange().events().hash());
  CHECK(replay.summary().pnl == live.summary().pnl);
  CHECK(replay.action_log().size() == live.action_log().size());
  CHECK(snapshot::summary_json(replay) == snapshot::summary_json(live));
}

TEST_CASE("a round with stop orders replays exactly from its JSON action log") {
  const GameConfig cfg = short_round(Difficulty::Hard, Scenario::None, 40);
  MarketSim live(21, cfg);
  Rng rng(3);
  int stops = 0;
  while (!live.finished()) {
    if (rng.chance(1, 50)) {
      const Qty pos = live.exchange().account(kPlayer).position;
      const Price last = live.exchange().mark();
      if (pos == 0) {
        live.player_submit(rng.chance(1, 2) ? Side::Buy : Side::Sell, OrderType::Market, 0, 2);
      } else {
        const auto r = pos > 0 ? live.player_submit(Side::Sell, OrderType::Stop, last - 3, pos)
                               : live.player_submit(Side::Buy, OrderType::Stop, last + 3, -pos);
        stops += r.ok();
      }
    }
    live.run_ticks(1);
  }
  REQUIRE(stops > 0);
  const auto log = snapshot::parse_actions(snapshot::actions_json(live.action_log()));
  REQUIRE(log);
  MarketSim replay(21, cfg, *log);
  while (!replay.finished()) replay.step(200);
  CHECK(replay.exchange().events().hash() == live.exchange().events().hash());
  CHECK(replay.summary().pnl == live.summary().pnl);
}

TEST_CASE("action log JSON rejects malformed input") {
  std::string err;
  CHECK(snapshot::parse_actions("[]")->empty());
  CHECK_FALSE(snapshot::parse_actions("[1,2,3]", &err));
  CHECK_FALSE(snapshot::parse_actions("[5,0,0,1,0,1,0, 4,0,0,1,0,1,0]", &err));  // out of order
  CHECK(err.find("order") != std::string::npos);
  CHECK_FALSE(snapshot::parse_actions("[0,9,0,1,0,1,0]", &err));
  CHECK_FALSE(snapshot::parse_actions("[0,0,0,4,0,1,0]", &err));  // unknown order type
  CHECK_FALSE(snapshot::parse_actions("{}", &err));
}

TEST_CASE("fair value series: one sample per second, withheld until the round ends") {
  MarketSim sim(21, short_round(Difficulty::Normal, Scenario::None, 20));
  sim.run_ticks(200);
  CHECK(snapshot::summary_json(sim).find("fairValues") == std::string::npos);
  CHECK(snapshot::summary_json(sim).find("fairValue") == std::string::npos);
  sim.run_to_end();
  const auto& fv = sim.fair_value_curve();
  CHECK(fv.size() == 21);
  CHECK(fv.back() == sim.fair_value());
  CHECK(snapshot::summary_json(sim).find("\"fairValues\":[") != std::string::npos);
}

TEST_CASE("full rounds stay zero-sum after every tick (difficulties x scenarios x seeds)") {
  int rounds = 0;
  std::uint64_t ticks = 0;
  for (Difficulty d : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard}) {
    for (Scenario s : {Scenario::None, Scenario::FlashCrash, Scenario::ShortSqueeze, Scenario::ThinLiquidity,
                       Scenario::NewsDay}) {
      for (std::uint64_t seed = 1; seed <= 2; ++seed) {
        MarketSim sim(seed * 1000 + static_cast<std::uint64_t>(s), short_round(d, s, 45));
        Rng rng(seed);
        while (!sim.finished()) {
          if (rng.chance(1, 30))
            sim.player_submit(rng.chance(1, 2) ? Side::Buy : Side::Sell, OrderType::Market, 0, 2);
          sim.run_ticks(1);
          REQUIRE(sim.exchange().zero_sum_holds());
          ++ticks;
        }
        for (ParticipantId p = 0; p < sim.exchange().participant_count(); ++p)
          REQUIRE(sim.exchange().account(p).position == 0);
        ++rounds;
      }
    }
  }
  CHECK(rounds == 30);
  CHECK(ticks > 50000);
}

TEST_CASE("round lifecycle: phases, settlement and summary") {
  MarketSim sim(42, short_round(Difficulty::Normal, Scenario::None, 40));
  CHECK(sim.phase() == Phase::Open);
  sim.player_submit(Side::Buy, OrderType::Market, 0, 1);
  sim.run_ticks(static_cast<std::uint64_t>(sim.round_us() / 1000 / sim.config().tick_ms) - 2);
  CHECK(sim.phase() == Phase::Settlement);
  sim.run_to_end();
  CHECK(sim.finished());
  CHECK(sim.phase() == Phase::Settled);

  const RoundSummary r = sim.summary();
  CHECK(r.fills >= 1);
  CHECK(r.bot_count == sim.bots().size());
  CHECK(r.settlement_price > 0);
  CHECK(r.pnl == sim.exchange().total_pnl(kPlayer));
  CHECK(sim.exchange().account(kPlayer).position == 0);
  CHECK(sim.equity_curve().size() >= 40);
  CHECK(sim.player_submit(Side::Buy, OrderType::Market, 0, 1).reject == RejectReason::RoundClosed);
}

TEST_CASE("flash crash liquidates a max-long player and fails the scenario goal") {
  const GameConfig cfg = make_config(Difficulty::Normal, Scenario::FlashCrash);
  MarketSim sim(1987, cfg);
  // Build a full-size long position just before the crash headline.
  const auto crash_tick = static_cast<std::uint64_t>(cfg.round_seconds * 40 / 100 * 1000 / cfg.tick_ms);
  sim.run_ticks(crash_tick - 200);
  for (int i = 0; i < 4 && sim.exchange().account(kPlayer).position < 4; ++i)
    sim.player_submit(Side::Buy, OrderType::Market, 0, 4 - sim.exchange().account(kPlayer).position);
  REQUIRE(sim.exchange().account(kPlayer).position == 4);
  sim.run_to_end();
  const RoundSummary r = sim.summary();
  CHECK(r.liquidations >= 1);
  CHECK_FALSE(r.goal_met);
  CHECK(r.pnl < 0);
}

TEST_CASE("engine stats are populated") {
  MarketSim sim(9, short_round(Difficulty::Normal, Scenario::None, 20));
  sim.run_to_end();
  const EngineStats s = sim.stats();
  CHECK(s.orders > 1000);
  CHECK(s.trades > 50);
  CHECK(s.events_per_sec > 0);
  CHECK(s.ticks == sim.ticks());
}
