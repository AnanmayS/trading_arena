#include "btb/bots.hpp"

#include <algorithm>
#include <cstdlib>

namespace btb {

const char* to_string(BotKind k) {
  switch (k) {
    case BotKind::MarketMaker: return "market maker";
    case BotKind::Momentum: return "momentum";
    case BotKind::MeanReversion: return "mean reversion";
    case BotKind::Noise: return "noise";
    case BotKind::Informed: return "informed";
  }
  return "?";
}

namespace {
template <class T>
T sign(T v) {
  return (v > 0) - (v < 0);
}
}  // namespace

// ------------------------------------------------------------ market maker

MarketMaker::MarketMaker(ParticipantId id, std::uint64_t seed, const MakerParams& p, Price initial)
    : Bot(id, seed), p_(p), est_fp_(initial * kFp), bids_(p.levels), asks_(p.levels) {
  bias_ = rng_.range(-1, 1);
}

void MarketMaker::pull(const MarketView& m, OrderSink& out) {
  for (auto* side : {&bids_, &asks_})
    for (Quote& q : *side) {
      if (m.is_live(q.id)) out.cancel(q.id);
      q = {};
    }
}

void MarketMaker::place(const MarketView& m, OrderSink& out, Quote& q, Side side, Price px, Qty qty) {
  if (m.is_live(q.id)) {
    if (q.price == px) return;  // unchanged: keep queue priority
    out.cancel(q.id);
  }
  q = {};
  const SubmitResult r = out.submit(side, OrderType::Limit, px, qty);
  if (r.resting > 0) q = {r.id, px};
}

bool MarketMaker::own_level(const MarketView& m, Price px) const {
  for (const auto* side : {&bids_, &asks_})
    for (const Quote& q : *side)
      if (q.price == px && m.is_live(q.id)) return true;
  return false;
}

void MarketMaker::on_tick(const MarketView& m, OrderSink& out) {
  const SimTime now = m.now();

  // Headline: lean the estimate by part of the move such news usually brings.
  // Major headlines also make the maker pull its quotes and step back.
  std::int32_t sentiment = 0;
  if (m.headline_time() != seen_headline_ && m.recent_headline(500'000, &sentiment)) {
    seen_headline_ = m.headline_time();
    static constexpr Price kTypicalMove[] = {0, 12, 35, 80};
    const Price move = kTypicalMove[std::abs(sentiment)] * p_.news_react_pct / 100;
    est_fp_ += (sentiment < 0 ? -move : move) * kFp;
    if (std::abs(sentiment) >= 2) {
      pull(m, out);
      pause_until_ = now + p_.pause_us;
    }
  }
  // Big move in the last second: someone knows something. Step back.
  if (!m.history().empty() && std::llabs(m.mark() - m.history().ago(1'000'000)) >= p_.pull_move &&
      now >= pause_until_) {
    pull(m, out);
    pause_until_ = now + p_.pause_us / 2;
  }

  // Learn from the tape: net aggressive buying lifts the estimate, selling
  // sinks it, and it drifts toward where the market actually trades.
  const std::int64_t flow = m.signed_volume() - seen_volume_;
  seen_volume_ = m.signed_volume();
  est_fp_ += flow * p_.flow_impact_fp;
  est_fp_ += (m.last_trade() * kFp - est_fp_) / p_.tape_follow_div;
  if (now < pause_until_) return;
  if (!rng_.chance(p_.requote_per_mille, 1000)) return;

  const Qty pos = position(m);
  const Price rv = m.history().realized_vol(5);
  const Price half = p_.half_spread + rv * p_.vol_widen_fp / kFp;
  const Price skew = -static_cast<Price>(pos) * p_.skew_fp / kFp;
  const Price center = (est_fp_ + kFp / 2) / kFp + skew + bias_;

  for (int i = 0; i < p_.levels; ++i) {
    const auto ui = static_cast<std::size_t>(i);
    const Qty qty = static_cast<Qty>(p_.size * (i + 1) + rng_.range(-1, 1));
    Price bid = center - half - i * p_.level_step;
    Price ask = center + half + i * p_.level_step;
    // Post-only behaviour: a maker never crosses the book, it slides its
    // quote behind the opposite touch instead (unless that touch is its own).
    if (m.has_ask() && bid >= m.best_ask() && !own_level(m, m.best_ask())) bid = m.best_ask() - 1 - i * p_.level_step;
    if (m.has_bid() && ask <= m.best_bid() && !own_level(m, m.best_bid())) ask = m.best_bid() + 1 + i * p_.level_step;
    // At the inventory limit, stop quoting the side that would add to it.
    if (pos < p_.max_inventory) {
      place(m, out, bids_[ui], Side::Buy, bid, std::max<Qty>(1, qty));
    } else if (m.is_live(bids_[ui].id)) {
      out.cancel(bids_[ui].id);
      bids_[ui] = {};
    }
    if (pos > -p_.max_inventory) {
      place(m, out, asks_[ui], Side::Sell, ask, std::max<Qty>(1, qty));
    } else if (m.is_live(asks_[ui].id)) {
      out.cancel(asks_[ui].id);
      asks_[ui] = {};
    }
  }
}

// ------------------------------------------------------------ momentum

void MomentumTrader::on_tick(const MarketView& m, OrderSink& out) {
  if (m.history().empty() || !rng_.chance(p_.act_per_mille, 1000)) return;
  const Qty pos = position(m);
  const Price move = m.mark() - m.history().ago(p_.lookback_us);

  if (pos != 0) {
    const bool reversed = pos > 0 ? move <= -p_.threshold / 2 : move >= p_.threshold / 2;
    if (reversed || m.now() - entered_ > p_.max_hold_us) {
      out.submit(pos > 0 ? Side::Sell : Side::Buy, OrderType::Market, 0, std::min(std::abs(pos), p_.size * 2));
      return;
    }
  }
  if (move >= p_.threshold && pos < p_.max_position) {
    if (pos == 0) entered_ = m.now();
    out.submit(Side::Buy, OrderType::Market, 0, p_.size);
  } else if (move <= -p_.threshold && pos > -p_.max_position) {
    if (pos == 0) entered_ = m.now();
    out.submit(Side::Sell, OrderType::Market, 0, p_.size);
  }
}

// ------------------------------------------------------------ mean reversion

void MeanReversionTrader::on_tick(const MarketView& m, OrderSink& out) {
  if (m.is_live(working_) && m.now() - placed_ > 3'000'000) {
    out.cancel(working_);
    working_ = kNoOrder;
  }
  if (m.history().size() < 50 || !rng_.chance(p_.act_per_mille, 1000)) return;
  if (m.is_live(working_)) return;

  const Qty pos = position(m);
  const Price dev = m.mark() - m.history().sma(p_.window_us);
  Side side;
  Qty qty;
  if (dev >= p_.threshold && pos > -p_.max_position) {
    side = Side::Sell;
    qty = p_.size;
  } else if (dev <= -p_.threshold && pos < p_.max_position) {
    side = Side::Buy;
    qty = p_.size;
  } else if (std::llabs(dev) <= p_.threshold / 3 && pos != 0) {
    side = pos > 0 ? Side::Sell : Side::Buy;
    qty = std::min(std::abs(pos), p_.size);
  } else {
    return;
  }
  // Join the touch on our side; fall back to one tick off the mark.
  Price px;
  if (side == Side::Buy)
    px = m.has_bid() ? m.best_bid() : m.mark() - 1;
  else
    px = m.has_ask() ? m.best_ask() : m.mark() + 1;
  const SubmitResult r = out.submit(side, OrderType::Limit, px, qty);
  if (r.resting > 0) {
    working_ = r.id;
    placed_ = m.now();
  }
}

// ------------------------------------------------------------ noise

void NoiseTrader::on_tick(const MarketView& m, OrderSink& out) {
  const SimTime now = m.now();
  std::erase_if(live_, [&](const Working& w) {
    if (!m.is_live(w.id)) return true;
    if (now < w.expires) return false;
    out.cancel(w.id);
    return true;
  });
  if (!rng_.chance(p_.act_per_mille, 1000) || live_.size() >= 6) return;

  const Qty pos = position(m);
  Side side = rng_.chance(1, 2) ? Side::Buy : Side::Sell;
  if (std::abs(pos) > 10 && rng_.chance(7, 10)) side = pos > 0 ? Side::Sell : Side::Buy;
  const Qty qty = static_cast<Qty>(rng_.range(1, p_.max_size));

  if (rng_.below(100) < p_.market_pct) {
    out.submit(side, OrderType::Market, 0, qty);
    return;
  }
  // Offsets skew small (min of two draws) so the inside of the book is busiest.
  const Price off = std::min(rng_.range(0, p_.max_offset), rng_.range(0, p_.max_offset));
  const Price px = side == Side::Buy ? m.mark() - off : m.mark() + off;
  const SubmitResult r = out.submit(side, OrderType::Limit, px, qty);
  if (r.resting > 0) live_.push_back({r.id, now + rng_.range(p_.min_life_us, p_.max_life_us)});
}

// ------------------------------------------------------------ informed

void InformedTrader::on_tick(const MarketView& m, OrderSink& out) {
  const SimTime now = m.now();
  if (now >= error_until_) {
    error_ = rng_.normal_fp16() * p_.noise / kFp;
    error_until_ = now + rng_.range(1'500'000, 4'000'000);
  }
  const Price est = fv_.value() + error_;
  const Price edge = est - m.mark();
  // Urgency grows with the edge: a big mispricing gets traded every tick.
  const std::uint32_t urgency = std::llabs(edge) >= 3 * p_.threshold ? 1000 : p_.act_per_mille;
  if (!rng_.chance(urgency, 1000)) return;

  const Qty pos = position(m);
  // Bigger edge, bigger size (up to 3x).
  const Qty qty = static_cast<Qty>(p_.size * std::min<Price>(3, std::max<Price>(1, std::llabs(edge) / p_.threshold)));

  if (edge >= p_.threshold && pos < p_.max_position) {
    out.submit(Side::Buy, OrderType::IOC, est - p_.threshold / 2, std::min(qty, p_.max_position - pos));
  } else if (edge <= -p_.threshold && pos > -p_.max_position) {
    out.submit(Side::Sell, OrderType::IOC, est + p_.threshold / 2, std::min(qty, p_.max_position + pos));
  } else if (std::llabs(edge) <= 1 && pos != 0 && rng_.chance(1, 4)) {
    // At fair value: take some risk off at the touch.
    const Side side = pos > 0 ? Side::Sell : Side::Buy;
    const Price px = side == Side::Sell ? (m.has_bid() ? m.best_bid() : est) : (m.has_ask() ? m.best_ask() : est);
    out.submit(side, OrderType::IOC, px, std::min(std::abs(pos), p_.size));
  }
}

}  // namespace btb
