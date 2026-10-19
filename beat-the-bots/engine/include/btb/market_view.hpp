// What a bot is allowed to see and do.
//
// MarketView exposes only public information: the book, trades, the mark price
// history and published headlines. The hidden fair value is deliberately not
// reachable from here; the informed trader gets its own noisy feed.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "btb/exchange.hpp"

namespace btb {

// Mark price sampled at a fixed interval (100 ms), for lookbacks and averages.
class PriceHistory {
 public:
  static constexpr SimTime kIntervalUs = 100'000;

  explicit PriceHistory(std::size_t capacity = 1 << 15) : buf_(capacity) {}

  void push(Price p) {
    buf_[count_ % buf_.size()] = p;
    ++count_;
  }
  std::size_t size() const { return count_ < buf_.size() ? count_ : buf_.size(); }
  bool empty() const { return count_ == 0; }
  Price latest() const { return buf_[(count_ - 1) % buf_.size()]; }

  // Price `us` ago (clamped to the oldest sample).
  Price ago(SimTime us) const {
    std::size_t back = static_cast<std::size_t>(us / kIntervalUs);
    if (back >= size()) back = size() - 1;
    return buf_[(count_ - 1 - back) % buf_.size()];
  }

  // Simple moving average over the last `us`, rounded to a tick.
  Price sma(SimTime us) const {
    std::size_t n = static_cast<std::size_t>(us / kIntervalUs);
    if (n > size()) n = size();
    if (n == 0) return latest();
    std::int64_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) sum += buf_[(count_ - 1 - i) % buf_.size()];
    return (sum + static_cast<std::int64_t>(n / 2)) / static_cast<std::int64_t>(n);
  }

  // Mean absolute 1-second move over the last `seconds`, in ticks.
  Price realized_vol(int seconds) const {
    const std::size_t step = static_cast<std::size_t>(1'000'000 / kIntervalUs);
    std::int64_t sum = 0;
    int n = 0;
    for (int s = 0; s < seconds; ++s) {
      const std::size_t a = static_cast<std::size_t>(s) * step;
      if (a + step >= size()) break;
      const Price p0 = buf_[(count_ - 1 - a) % buf_.size()];
      const Price p1 = buf_[(count_ - 1 - a - step) % buf_.size()];
      sum += std::llabs(p0 - p1);
      ++n;
    }
    return n ? sum / n : 0;
  }

 private:
  std::vector<Price> buf_;
  std::size_t count_ = 0;
};

struct PublicNews {
  SimTime time_us = -1;  // -1: no headline yet
  std::int32_t sentiment = 0;
};

class MarketView {
 public:
  MarketView(const Exchange& ex, const PriceHistory& hist, const PublicNews& news)
      : ex_(ex), hist_(hist), news_(news) {}

  SimTime now() const { return ex_.now(); }
  bool has_bid() const { return ex_.book().has_bid(); }
  bool has_ask() const { return ex_.book().has_ask(); }
  Price best_bid() const { return ex_.book().best_bid(); }
  Price best_ask() const { return ex_.book().best_ask(); }
  Qty qty_at(Price p) const { return ex_.book().qty_at(p); }
  Price mark() const { return ex_.mark(); }
  Price last_trade() const { return ex_.has_last_trade() ? ex_.last_trade() : ex_.mark(); }
  const PriceHistory& history() const { return hist_; }
  std::int64_t signed_volume() const { return ex_.signed_volume(); }

  Qty position(ParticipantId p) const { return ex_.account(p).position; }
  bool is_live(OrderId id) const { return id != kNoOrder && ex_.book().find(id) != nullptr; }
  Price order_price(OrderId id) const {
    const Order* o = ex_.book().find(id);
    return o ? o->price : 0;
  }

  // Most recent headline, if it was published within the last `us`.
  bool recent_headline(SimTime us, std::int32_t* sentiment) const {
    if (news_.time_us < 0 || now() - news_.time_us > us) return false;
    *sentiment = news_.sentiment;
    return true;
  }
  SimTime headline_time() const { return news_.time_us; }

 private:
  const Exchange& ex_;
  const PriceHistory& hist_;
  const PublicNews& news_;
};

// The only way a bot can act on the market.
class OrderSink {
 public:
  virtual ~OrderSink() = default;
  virtual SubmitResult submit(Side side, OrderType type, Price price, Qty qty) = 0;
  virtual bool cancel(OrderId id) = 0;
  virtual void cancel_all() = 0;
};

}  // namespace btb
