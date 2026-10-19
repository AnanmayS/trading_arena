// Bot archetypes. Each bot is a participant with its own RNG stream and acts
// once per simulation tick through MarketView (read) and OrderSink (write).
#pragma once

#include <cstdint>
#include <vector>

#include "btb/config.hpp"
#include "btb/fair_value.hpp"
#include "btb/market_view.hpp"
#include "btb/rng.hpp"

namespace btb {

enum class BotKind : std::uint8_t { MarketMaker = 1, Momentum = 2, MeanReversion = 3, Noise = 4, Informed = 5 };
const char* to_string(BotKind k);

class Bot {
 public:
  Bot(ParticipantId id, std::uint64_t seed) : id_(id), rng_(seed) {}
  virtual ~Bot() = default;

  virtual void on_tick(const MarketView& m, OrderSink& out) = 0;
  virtual BotKind kind() const = 0;
  ParticipantId id() const { return id_; }

 protected:
  Qty position(const MarketView& m) const { return m.position(id_); }

  ParticipantId id_;
  Rng rng_;
};

// Quotes both sides around an estimate that follows the tape, skews quotes
// against its inventory, widens with volatility and pulls everything after a
// big move or a major headline.
class MarketMaker final : public Bot {
 public:
  MarketMaker(ParticipantId id, std::uint64_t seed, const MakerParams& p, Price initial);
  void on_tick(const MarketView& m, OrderSink& out) override;
  BotKind kind() const override { return BotKind::MarketMaker; }
  Price estimate() const { return (est_fp_ + kFp / 2) / kFp; }

 private:
  struct Quote {
    OrderId id = kNoOrder;
    Price price = 0;
  };
  void place(const MarketView& m, OrderSink& out, Quote& q, Side side, Price px, Qty qty);
  void pull(const MarketView& m, OrderSink& out);

  MakerParams p_;
  std::int64_t est_fp_;
  Price bias_;  // small per-bot offset so makers do not all quote identically
  SimTime pause_until_ = 0;
  SimTime wide_until_ = 0;
  SimTime seen_headline_ = -1;
  Price pending_news_ = 0;     // repricing still to apply at digest time
  SimTime news_digest_ = -1;
  std::int64_t seen_volume_ = 0;
  std::int64_t own_flow_ = 0;  // our own aggressive volume since the last tick
  std::vector<Quote> bids_, asks_;
};

// Chases recent moves with market orders; exits when the move stalls.
class MomentumTrader final : public Bot {
 public:
  MomentumTrader(ParticipantId id, std::uint64_t seed, const MomentumParams& p) : Bot(id, seed), p_(p) {}
  void on_tick(const MarketView& m, OrderSink& out) override;
  BotKind kind() const override { return BotKind::Momentum; }

 private:
  MomentumParams p_;
  SimTime entered_ = 0;
};

// Fades deviations from a moving average with passive orders at the touch.
class MeanReversionTrader final : public Bot {
 public:
  MeanReversionTrader(ParticipantId id, std::uint64_t seed, const MeanRevParams& p) : Bot(id, seed), p_(p) {}
  void on_tick(const MarketView& m, OrderSink& out) override;
  BotKind kind() const override { return BotKind::MeanReversion; }

 private:
  MeanRevParams p_;
  OrderId working_ = kNoOrder;
  SimTime placed_ = 0;
};

// Random small orders, mostly passive, each with a random lifetime.
class NoiseTrader final : public Bot {
 public:
  NoiseTrader(ParticipantId id, std::uint64_t seed, const NoiseParams& p) : Bot(id, seed), p_(p) {}
  void on_tick(const MarketView& m, OrderSink& out) override;
  BotKind kind() const override { return BotKind::Noise; }

 private:
  struct Working {
    OrderId id;
    SimTime expires;
  };
  NoiseParams p_;
  std::vector<Working> live_;
  SimTime seen_headline_ = -1;
};

// Sees a noisy version of the hidden fair value and trades toward it. This is
// what makes order flow informative: when it buys, the price is going up.
// Once the price has caught up it works its position back down passively, so
// it is an arbitrageur rather than a gambler holding inventory into news.
class InformedTrader final : public Bot {
 public:
  InformedTrader(ParticipantId id, std::uint64_t seed, const InformedParams& p, const FairValue& fv)
      : Bot(id, seed), p_(p), fv_(fv) {}
  void on_tick(const MarketView& m, OrderSink& out) override;
  BotKind kind() const override { return BotKind::Informed; }

 private:
  InformedParams p_;
  const FairValue& fv_;
  Price error_ = 0;  // persistent estimation error, redrawn every few seconds
  SimTime error_until_ = 0;
  OrderId unwind_ = kNoOrder;  // passive order working the position down
  SimTime unwind_at_ = 0;
};

}  // namespace btb
