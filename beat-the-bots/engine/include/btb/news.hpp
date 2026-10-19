// Headlines and the per-round news schedule.
//
// Each market-moving headline is published a little before its price jump
// (how much before depends on difficulty), so a player who reads the feed
// can react before the bots fully reprice.
#pragma once

#include <cstdint>
#include <vector>

#include "btb/config.hpp"
#include "btb/rng.hpp"

namespace btb {

struct Headline {
  const char* text;
  std::int8_t sentiment;  // -3 (very bearish) .. +3 (very bullish), 0 = noise
};

// The fixed headline catalog; ids are indices into it.
const Headline* headline_catalog(std::size_t* count);

struct NewsItem {
  SimTime release_us = 0;  // when the headline appears
  SimTime jump_us = 0;     // when fair value moves
  std::uint32_t headline = 0;
  std::int32_t sentiment = 0;
  Price jump = 0;  // ticks, signed
  // Optional slow drift in ticks/second (fp) starting at the jump.
  std::int64_t drift_fp = 0;
  SimTime drift_us = 0;
};

// Builds the round's schedule, sorted by release time.
std::vector<NewsItem> make_news_schedule(const GameConfig& cfg, Rng& rng);

}  // namespace btb
