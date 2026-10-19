// Game configuration: difficulty presets, scenarios and every tunable knob of
// the market simulation. Fixed-point fields use a 16-bit fraction ("_fp").
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "btb/contract.hpp"
#include "btb/exchange.hpp"
#include "btb/types.hpp"

namespace btb {

enum class Difficulty : std::uint8_t { Easy = 0, Normal = 1, Hard = 2 };
enum class Scenario : std::uint8_t { None = 0, FlashCrash = 1, ShortSqueeze = 2, ThinLiquidity = 3, NewsDay = 4 };

inline constexpr std::int64_t kFp = 65536;  // 1.0 in 16.16 fixed point

struct MakerParams {
  int count = 4;
  Price half_spread = 1;      // ticks from estimate to best quote
  int levels = 3;             // quote levels per side
  Price level_step = 1;       // ticks between levels
  Qty size = 4;               // size at the best level; deeper levels add `size` each
  std::int64_t skew_fp = kFp / 4;  // quote shift in ticks per contract of inventory
  Qty max_inventory = 30;
  std::int64_t vol_widen_fp = kFp / 3;  // extra half-spread per tick of realised vol
  Price pull_move = 14;       // pull quotes if the mark moves this many ticks in 1s
  SimTime pause_us = 1'200'000;
  std::uint32_t requote_per_mille = 600;  // chance per tick to refresh quotes
  // How the estimate learns. Aggressive buying anywhere on the tape raises it
  // by flow_impact ticks per contract (a Kyle-style price impact); it also
  // drifts toward the last trade, and jumps on headlines by a fraction of the
  // move that headline's sentiment usually brings.
  std::int64_t flow_impact_fp = kFp * 15 / 100;
  int tape_follow_div = 32;
  int news_react_pct = 75;
};

struct MomentumParams {
  int count = 6;
  SimTime lookback_us = 3'000'000;
  Price threshold = 6;
  Qty size = 2;
  Qty max_position = 6;
  std::uint32_t act_per_mille = 30;
  SimTime max_hold_us = 20'000'000;
};

struct MeanRevParams {
  int count = 5;
  SimTime window_us = 30'000'000;
  Price threshold = 10;
  Qty size = 2;
  Qty max_position = 8;
  std::uint32_t act_per_mille = 30;
};

struct NoiseParams {
  int count = 16;
  std::uint32_t act_per_mille = 60;
  Qty max_size = 3;
  Price max_offset = 12;  // passive orders rest up to this far from the mark
  std::uint32_t market_pct = 20;
  SimTime min_life_us = 1'000'000;
  SimTime max_life_us = 8'000'000;
};

struct InformedParams {
  int count = 1;
  Price noise = 4;      // stddev of its fair-value estimate, ticks
  Price threshold = 4;  // minimum edge before trading
  Qty size = 3;
  Qty max_position = 200;
  std::uint32_t act_per_mille = 120;
};

struct MarketParams {
  Price initial_price = 20000;          // 5000.00 index points
  std::int64_t vol_fp = 2 * kFp;        // ticks per sqrt(second)
  std::int64_t open_vol_mult_fp = kFp * 6 / 10;   // calm open
  std::int64_t close_vol_mult_fp = kFp * 18 / 10; // hot close
};

struct NewsParams {
  int headlines = 6;
  int jump_pct = 100;               // scales every jump
  SimTime lead_min_us = 500'000;    // headline appears this long before the jump
  SimTime lead_max_us = 1'500'000;
  bool mid_round_shock = true;
};

struct GameConfig {
  Difficulty difficulty = Difficulty::Normal;
  Scenario scenario = Scenario::None;
  std::uint32_t round_seconds = 300;
  std::uint32_t tick_ms = 25;
  std::uint32_t settlement_window_seconds = 10;
  EventFilter events;
  ContractSpec contract;
  MarketParams market;
  NewsParams news;
  MakerParams makers;
  MomentumParams momentum;
  MeanRevParams mean_rev;
  NoiseParams noise;
  InformedParams informed;
};

// Builds the preset for a difficulty, then applies the scenario on top.
GameConfig make_config(Difficulty d, Scenario s = Scenario::None);

// Parses {"difficulty":"hard","scenario":"flash_crash","round_seconds":120,...}.
// Unknown keys are ignored; invalid values produce an error.
std::optional<GameConfig> parse_config(std::string_view json, std::string* error = nullptr);

const char* to_string(Difficulty d);
const char* to_string(Scenario s);
std::optional<Difficulty> difficulty_from(std::string_view s);
std::optional<Scenario> scenario_from(std::string_view s);

struct ScenarioInfo {
  Scenario id;
  const char* key;
  const char* title;
  const char* goal;
  std::uint64_t seed;
  Difficulty difficulty;
};
// All playable scenarios (excludes Scenario::None).
const ScenarioInfo* scenarios(std::size_t* count);

}  // namespace btb
