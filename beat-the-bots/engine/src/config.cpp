#include "btb/config.hpp"

#include "btb/json.hpp"

namespace btb {

namespace {

constexpr ScenarioInfo kScenarios[] = {
    {Scenario::FlashCrash, "flash_crash", "Flash Crash",
     "Survive without getting liquidated.", 1987, Difficulty::Normal},
    {Scenario::ShortSqueeze, "short_squeeze", "Short Squeeze",
     "Finish the round with a profit.", 2021, Difficulty::Normal},
    {Scenario::ThinLiquidity, "thin_liquidity", "Thin Liquidity",
     "Make money when the book is nearly empty.", 404, Difficulty::Hard},
    {Scenario::NewsDay, "news_day", "News Day",
     "Trade the headlines: finish up $1,000 or more.", 777, Difficulty::Normal},
};

void apply_difficulty(GameConfig& c, Difficulty d) {
  c.difficulty = d;
  switch (d) {
    case Difficulty::Easy:
      // Calmer market, generous headline lead, informed flow is slow and noisy.
      c.market.vol_fp = kFp * 11 / 10;
      c.makers.half_spread = 1;
      c.makers.size = 6;
      c.news.lead_min_us = 1'500'000;
      c.news.lead_max_us = 2'500'000;
      c.news.jump_pct = 80;
      c.momentum.act_per_mille = 25;
      break;
    case Difficulty::Normal:
      break;  // the struct defaults are the normal preset
    case Difficulty::Hard:
      // Wider, jumpier market; informed trader moves price to value fast and
      // headlines land with the jump, leaving almost no time to react.
      c.market.vol_fp = kFp * 19 / 10;
      c.makers.half_spread = 2;
      c.makers.size = 3;
      c.makers.pause_us = 2'000'000;
      c.informed.noise = 1;
      c.informed.act_per_mille = 220;
      c.informed.size = 4;
      c.news.lead_min_us = 0;
      c.news.lead_max_us = 400'000;
      c.news.jump_pct = 120;
      c.momentum.act_per_mille = 60;
      c.momentum.size = 3;
      break;
  }
}

void apply_scenario(GameConfig& c, Scenario s) {
  c.scenario = s;
  switch (s) {
    case Scenario::None:
      break;
    case Scenario::FlashCrash:
      // The crash itself is scripted in the news schedule; makers pull hard.
      c.makers.pause_us = 4'000'000;
      c.makers.pull_move = 10;
      c.news.headlines = 4;
      break;
    case Scenario::ShortSqueeze:
      c.momentum.count = 9;
      c.momentum.act_per_mille = 45;
      c.news.headlines = 4;
      break;
    case Scenario::ThinLiquidity:
      c.makers.count = 2;
      c.makers.size = 1;
      c.makers.levels = 2;
      c.makers.half_spread = 3;
      c.makers.level_step = 2;
      c.noise.count = 8;
      c.noise.max_size = 1;
      break;
    case Scenario::NewsDay:
      c.news.headlines = 14;
      c.news.jump_pct = 110;
      break;
  }
}

}  // namespace

GameConfig make_config(Difficulty d, Scenario s) {
  GameConfig c;
  apply_difficulty(c, d);
  apply_scenario(c, s);
  return c;
}

std::optional<GameConfig> parse_config(std::string_view text, std::string* error) {
  const auto obj = json::FlatObject::parse(text, error);
  if (!obj) return std::nullopt;
  auto fail = [&](const std::string& msg) -> std::optional<GameConfig> {
    if (error) *error = msg;
    return std::nullopt;
  };

  Difficulty d = Difficulty::Normal;
  Scenario s = Scenario::None;
  if (auto v = obj->string("difficulty")) {
    const auto parsed = difficulty_from(*v);
    if (!parsed) return fail("unknown difficulty: " + *v);
    d = *parsed;
  }
  if (auto v = obj->string("scenario")) {
    const auto parsed = scenario_from(*v);
    if (!parsed) return fail("unknown scenario: " + *v);
    s = *parsed;
  }
  GameConfig c = make_config(d, s);

  if (auto v = obj->integer("round_seconds")) {
    if (*v < 15 || *v > 3600) return fail("round_seconds must be 15..3600");
    c.round_seconds = static_cast<std::uint32_t>(*v);
  }
  if (auto v = obj->integer("tick_ms")) {
    if (*v < 5 || *v > 250) return fail("tick_ms must be 5..250");
    c.tick_ms = static_cast<std::uint32_t>(*v);
  }
  if (auto v = obj->boolean("bot_order_events")) c.events.bot_order_events = *v;
  if (auto v = obj->boolean("book_deltas")) c.events.book_deltas = *v;
  return c;
}

const char* to_string(Difficulty d) {
  switch (d) {
    case Difficulty::Easy: return "easy";
    case Difficulty::Normal: return "normal";
    case Difficulty::Hard: return "hard";
  }
  return "normal";
}

const char* to_string(Scenario s) {
  if (s == Scenario::None) return "none";
  for (const auto& info : kScenarios)
    if (info.id == s) return info.key;
  return "none";
}

std::optional<Difficulty> difficulty_from(std::string_view s) {
  if (s == "easy") return Difficulty::Easy;
  if (s == "normal") return Difficulty::Normal;
  if (s == "hard") return Difficulty::Hard;
  return std::nullopt;
}

std::optional<Scenario> scenario_from(std::string_view s) {
  if (s == "none" || s.empty()) return Scenario::None;
  for (const auto& info : kScenarios)
    if (s == info.key) return info.id;
  return std::nullopt;
}

const ScenarioInfo* scenarios(std::size_t* count) {
  *count = sizeof(kScenarios) / sizeof(kScenarios[0]);
  return kScenarios;
}

}  // namespace btb
