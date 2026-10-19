// Central limit order book with price-time priority.
//
// Layout, chosen for cache behaviour and zero allocation on the hot path:
//   * Price levels live in one contiguous array indexed by (price - base). A
//     level never holds bids and asks at the same time (the book is never
//     crossed), so a single array serves both sides.
//   * Each level is an intrusive doubly linked FIFO of orders. Links are 32-bit
//     indices into a preallocated order pool rather than pointers.
//   * One bitset per side marks non-empty levels, so finding the next best
//     price after a level empties is a count-leading/trailing-zeros scan.
//   * OrderId -> pool index goes through a flat open-addressing hash map.
//
// The book knows nothing about accounts or risk. It reports what happened
// through a MatchListener; the Exchange layer turns that into positions, P&L
// and events.
#pragma once

#include <cstdint>
#include <vector>

#include "btb/flat_map.hpp"
#include "btb/types.hpp"

namespace btb {

inline constexpr std::uint32_t kNil = 0xFFFFFFFFu;

struct Order {
  OrderId id = kNoOrder;
  Price price = 0;
  Qty qty = 0;  // remaining open quantity
  ParticipantId owner = 0;
  Side side = Side::Buy;
  std::uint32_t prev = kNil;  // intrusive FIFO links (pool indices)
  std::uint32_t next = kNil;
};

struct OrderRequest {
  ParticipantId owner = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;  // ignored for market orders
  Qty qty = 0;
};

struct SubmitResult {
  OrderId id = kNoOrder;
  RejectReason reject = RejectReason::None;
  Qty filled = 0;
  Qty resting = 0;
  bool ok() const { return reject == RejectReason::None; }
};

// Callbacks fired synchronously from inside submit()/cancel(), in the exact
// order things happen. Default implementations do nothing.
class MatchListener {
 public:
  virtual ~MatchListener() = default;
  // An order passed validation and was assigned an id (before any matching).
  virtual void on_accept(OrderId, const OrderRequest&) {}
  // One maker order traded against the incoming taker order.
  // `maker.qty` is the maker's remaining quantity after this fill.
  virtual void on_trade(const Order& /*maker*/, OrderId /*taker_id*/, ParticipantId /*taker*/,
                        Side /*aggressor*/, Price, Qty) {}
  // An order left the book (or never rested) with open quantity.
  virtual void on_cancel(OrderId, ParticipantId, Side, Price, Qty /*remaining*/, CancelReason) {}
  // Total resting quantity at a price changed.
  virtual void on_level(Side, Price, Qty /*total*/) {}
};

struct BookConfig {
  Price initial_mid = 20000;            // ticks; the price window is centred here
  std::uint32_t window_ticks = 8192;    // addressable prices, rounded up to 64
  std::uint32_t max_orders = 1u << 18;  // order pool capacity
};

struct LevelView {
  Price price;
  Qty qty;
  std::uint32_t orders;
};

class OrderBook {
 public:
  explicit OrderBook(BookConfig cfg = {}, MatchListener* listener = nullptr);

  void set_listener(MatchListener* l) { listener_ = l ? l : &null_listener_; }

  SubmitResult submit(const OrderRequest& req);
  // Returns false if the id is unknown (already filled or cancelled).
  bool cancel(OrderId id, CancelReason reason = CancelReason::User);
  // Cancels every resting order of one participant; returns how many.
  std::uint32_t cancel_all(ParticipantId owner, CancelReason reason = CancelReason::User);

  bool has_bid() const { return best_bid_ >= 0; }
  bool has_ask() const { return best_ask_ < static_cast<std::int64_t>(window_); }
  Price best_bid() const { return base_ + best_bid_; }  // valid only if has_bid()
  Price best_ask() const { return base_ + best_ask_; }  // valid only if has_ask()
  Qty qty_at(Price p) const;

  // Fills `out` with up to `max_levels` levels from the best price outward.
  std::uint32_t depth(Side side, LevelView* out, std::uint32_t max_levels) const;

  const Order* find(OrderId id) const;
  std::uint32_t order_count() const { return ids_.size(); }
  Price base() const { return base_; }
  std::uint32_t window() const { return window_; }

 private:
  struct Level {
    std::uint32_t head = kNil;
    std::uint32_t tail = kNil;
    Qty total = 0;
    std::uint32_t count = 0;
  };

  bool in_window(Price p) const { return p >= base_ && p < base_ + static_cast<Price>(window_); }
  bool ensure_in_window(Price p);
  void recenter(Price lo, Price hi);

  Qty match(OrderId taker_id, const OrderRequest& req, Price limit);
  void rest(OrderId id, const OrderRequest& req, Qty qty);
  void unlink(std::uint32_t idx);
  void release(std::uint32_t idx);

  void set_bit(Side s, std::uint32_t i);
  void clear_bit(Side s, std::uint32_t i);

  MatchListener null_listener_;
  MatchListener* listener_;

  Price base_ = 0;
  std::uint32_t window_ = 0;
  std::vector<Level> levels_;
  std::vector<std::uint64_t> bid_bits_;
  std::vector<std::uint64_t> ask_bits_;
  std::int64_t best_bid_ = -1;  // level index, -1 when empty
  std::int64_t best_ask_ = 0;   // level index, window_ when empty

  std::vector<Order> pool_;
  std::uint32_t free_head_ = kNil;
  OrderIdMap ids_;
  OrderId next_id_ = 1;
  std::vector<OrderId> scratch_;  // reused by cancel_all
};

}  // namespace btb
