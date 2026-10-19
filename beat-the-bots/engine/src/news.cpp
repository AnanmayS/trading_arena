#include "btb/news.hpp"

#include <algorithm>

namespace btb {

namespace {

// Ids are positions in this table and are part of the deterministic event
// stream, so append new headlines at the end.
constexpr Headline kHeadlines[] = {
    // 0..: very bullish
    {"Fed signals surprise rate cut", 3},
    {"Ceasefire announced; risk assets surge", 3},
    {"Blowout jobs report: payrolls double estimates", 3},
    // 3..: bullish
    {"Mega-cap earnings beat across the board", 2},
    {"Inflation cools more than expected", 2},
    {"Rumor: sovereign fund building a large long position", 2},
    // 6..: mildly bullish
    {"Consumer confidence ticks higher", 1},
    {"Retail sales slightly above forecast", 1},
    {"Strategist raises year-end index target", 1},
    // 9..: noise
    {"Fed chair to speak at a conference later today", 0},
    {"Trading volumes light ahead of the holiday", 0},
    {"Exchange reports record message traffic", 0},
    {"Analysts split on the outlook for next quarter", 0},
    {"Treasury auction results in line with expectations", 0},
    // 14..: mildly bearish
    {"Manufacturing PMI slips below forecast", -1},
    {"Oil climbs on supply worries", -1},
    {"Bond yields edge higher", -1},
    // 17..: bearish
    {"Inflation hotter than expected", -2},
    {"Rumor: large fund liquidating its equity book", -2},
    {"Tech giant misses earnings, guides lower", -2},
    // 20..: very bearish
    {"Fed surprise hike: 75 basis points", -3},
    {"Major bank halts withdrawals", -3},
    {"Flash crash in bond market spills into equities", -3},
    // 23..: scenario specials
    {"Sell orders overwhelm the book; circuit breakers eyed", -3},
    {"Short interest at record high; shorts scramble to cover", 3},
    {"Bargain hunters step in after the plunge", 2},
    {"Short sellers scramble as losses mount", 2},
    {"Margin calls force another wave of short covering", 2},
    {"Squeeze accelerates: shorts capitulate", 3},
};

constexpr std::uint32_t kRegular = 23;  // ids past this are scripted specials

// Picks a headline of the given sentiment, preferring ones not used yet.
std::uint32_t pick(Rng& rng, int sentiment, std::uint32_t& used_mask) {
  std::uint32_t fresh[8], all[8];
  std::uint32_t nf = 0, na = 0;
  for (std::uint32_t i = 0; i < kRegular; ++i) {
    if (kHeadlines[i].sentiment != sentiment) continue;
    all[na++] = i;
    if (!(used_mask & (1u << i))) fresh[nf++] = i;
  }
  const std::uint32_t id = nf ? fresh[rng.below(nf)] : all[rng.below(na)];
  used_mask |= 1u << id;
  return id;
}

// Jump size range in ticks by |sentiment|, before difficulty scaling.
Price jump_ticks(Rng& rng, int sentiment, int pct) {
  static constexpr Price lo[] = {0, 8, 24, 60};
  static constexpr Price hi[] = {0, 18, 48, 110};
  const int a = sentiment < 0 ? -sentiment : sentiment;
  const Price t = rng.range(lo[a], hi[a]) * pct / 100;
  return sentiment < 0 ? -t : t;
}

}  // namespace

const Headline* headline_catalog(std::size_t* count) {
  *count = sizeof(kHeadlines) / sizeof(kHeadlines[0]);
  return kHeadlines;
}

std::vector<NewsItem> make_news_schedule(const GameConfig& cfg, Rng& rng) {
  const SimTime round_us = static_cast<SimTime>(cfg.round_seconds) * 1'000'000;
  const SimTime start = std::min<SimTime>(15'000'000, round_us / 10);
  const SimTime end = round_us - std::min<SimTime>(15'000'000, round_us / 8);
  const NewsParams& np = cfg.news;
  std::vector<NewsItem> items;
  std::uint32_t used = 0;
  items.reserve(static_cast<std::size_t>(np.headlines) + 4);

  auto add = [&](SimTime at, int sentiment, std::uint32_t headline) {
    NewsItem it;
    it.release_us = at;
    it.headline = headline;
    it.sentiment = sentiment;
    it.jump = jump_ticks(rng, sentiment, np.jump_pct);
    it.jump_us = at + (sentiment ? rng.range(np.lead_min_us, np.lead_max_us) : 0);
    items.push_back(it);
    return &items.back();
  };

  // Scripted scenario events.
  switch (cfg.scenario) {
    case Scenario::FlashCrash: {
      const SimTime t = round_us * 40 / 100;
      add(t, -3, 23)->jump = -(180 * np.jump_pct / 100);
      add(t + 25'000'000, 2, 25)->jump = 90 * np.jump_pct / 100;
      break;
    }
    case Scenario::ShortSqueeze: {
      // A rolling squeeze: one headline after another, each forcing more
      // covering, with momentum bots piling in between.
      add(round_us * 30 / 100, 3, 24)->jump = 30;
      static constexpr std::uint32_t kWaves[] = {26, 27, 26, 28, 27};
      SimTime t = round_us * 30 / 100;
      for (const std::uint32_t h : kWaves) {
        t += round_us * 5 / 100 + rng.range(0, round_us * 2 / 100);
        add(t, kHeadlines[h].sentiment, h)->jump = rng.range(14, 28) * np.jump_pct / 100;
      }
      break;
    }
    default:
      break;
  }

  // The mid-round shock every normal round builds towards.
  if (np.mid_round_shock && cfg.scenario == Scenario::None) {
    const int s = rng.chance(1, 2) ? 3 : -3;
    add(round_us * 45 / 100 + rng.range(0, round_us * 15 / 100), s, pick(rng, s, used));
  }

  // Background headlines at random times: about a third are pure noise.
  for (int i = 0; i < np.headlines; ++i) {
    const SimTime at = rng.range(start, end);
    int s = 0;
    if (!rng.chance(1, 3)) {
      const int mag = rng.chance(3, 5) ? 1 : 2;
      s = rng.chance(1, 2) ? mag : -mag;
    }
    add(at, s, pick(rng, s, used));
  }

  std::sort(items.begin(), items.end(), [](const NewsItem& a, const NewsItem& b) {
    return a.release_us != b.release_us ? a.release_us < b.release_us : a.headline < b.headline;
  });
  return items;
}

}  // namespace btb
