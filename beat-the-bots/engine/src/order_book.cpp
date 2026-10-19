#include "btb/order_book.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace btb {

namespace {

// Index of the lowest set bit at or above `from`, or `end` if none.
std::int64_t next_set(const std::vector<std::uint64_t>& bits, std::int64_t from, std::int64_t end) {
  if (from >= end) return end;
  if (from < 0) from = 0;
  std::size_t w = static_cast<std::size_t>(from >> 6);
  std::uint64_t word = bits[w] & (~0ull << (from & 63));
  for (;;) {
    if (word) return static_cast<std::int64_t>(w * 64 + std::countr_zero(word));
    if (++w == bits.size()) return end;
    word = bits[w];
  }
}

// Index of the highest set bit at or below `from`, or -1 if none.
std::int64_t prev_set(const std::vector<std::uint64_t>& bits, std::int64_t from) {
  if (from < 0) return -1;
  std::size_t w = static_cast<std::size_t>(from >> 6);
  const int bit = static_cast<int>(from & 63);
  std::uint64_t word = bits[w] & (bit == 63 ? ~0ull : ((1ull << (bit + 1)) - 1));
  for (;;) {
    if (word) return static_cast<std::int64_t>(w * 64 + 63 - std::countl_zero(word));
    if (w == 0) return -1;
    word = bits[--w];
  }
}

}  // namespace

OrderBook::OrderBook(BookConfig cfg, MatchListener* listener)
    : listener_(listener ? listener : &null_listener_), ids_(cfg.max_orders) {
  window_ = (std::max<std::uint32_t>(cfg.window_ticks, 64) + 63) & ~63u;
  base_ = cfg.initial_mid - static_cast<Price>(window_ / 2);
  levels_.assign(window_, Level{});
  bid_bits_.assign(window_ / 64, 0);
  ask_bits_.assign(window_ / 64, 0);
  best_bid_ = -1;
  best_ask_ = window_;

  pool_.resize(cfg.max_orders);
  for (std::uint32_t i = 0; i < cfg.max_orders; ++i) pool_[i].next = (i + 1 < cfg.max_orders) ? i + 1 : kNil;
  free_head_ = cfg.max_orders ? 0 : kNil;
}

SubmitResult OrderBook::submit(const OrderRequest& req) {
  SubmitResult res;
  if (req.qty <= 0) {
    res.reject = RejectReason::InvalidQty;
    return res;
  }
  if (req.type != OrderType::Market) {
    if (req.price <= 0) {
      res.reject = RejectReason::InvalidPrice;
      return res;
    }
    if (!ensure_in_window(req.price)) {
      res.reject = RejectReason::PriceOutOfRange;
      return res;
    }
  }
  // Only a limit order can need a pool slot; check before accepting so a
  // reject never leaves a half-processed order behind.
  if (req.type == OrderType::Limit && free_head_ == kNil) {
    res.reject = RejectReason::PoolExhausted;
    return res;
  }

  res.id = next_id_++;
  listener_->on_accept(res.id, req);

  Price limit = req.price;
  if (req.type == OrderType::Market)
    limit = req.side == Side::Buy ? std::numeric_limits<Price>::max() : std::numeric_limits<Price>::min();

  res.filled = match(res.id, req, limit);
  const Qty left = req.qty - res.filled;
  if (left > 0) {
    if (req.type == OrderType::Limit) {
      rest(res.id, req, left);
      res.resting = left;
    } else {
      listener_->on_cancel(res.id, req.owner, req.side, req.price, left, CancelReason::Unfilled);
    }
  }
  return res;
}

Qty OrderBook::match(OrderId taker_id, const OrderRequest& req, Price limit) {
  const bool buy = req.side == Side::Buy;
  const Side maker_side = opposite(req.side);
  Qty left = req.qty;

  while (left > 0) {
    const std::int64_t li = buy ? best_ask_ : best_bid_;
    if (buy ? li >= static_cast<std::int64_t>(window_) : li < 0) break;
    const Price px = base_ + li;
    if (buy ? px > limit : px < limit) break;

    Level& lv = levels_[static_cast<std::size_t>(li)];
    std::uint32_t idx = lv.head;
    while (idx != kNil && left > 0) {
      Order& o = pool_[idx];
      const std::uint32_t nxt = o.next;
      if (o.owner == req.owner) {
        // Self-trade prevention: the resting order is cancelled and the
        // incoming order keeps walking the book.
        const Qty q = o.qty;
        const OrderId oid = o.id;
        unlink(idx);
        ids_.erase(oid);
        release(idx);
        listener_->on_cancel(oid, req.owner, maker_side, px, q, CancelReason::SelfTrade);
      } else {
        const Qty q = std::min(left, o.qty);
        o.qty -= q;
        lv.total -= q;
        left -= q;
        listener_->on_trade(o, taker_id, req.owner, req.side, px, q);
        if (o.qty == 0) {
          ids_.erase(o.id);
          unlink(idx);
          release(idx);
        }
      }
      idx = nxt;
    }
    listener_->on_level(maker_side, px, lv.total);

    if (lv.count == 0) {
      clear_bit(maker_side, static_cast<std::uint32_t>(li));
      if (buy)
        best_ask_ = next_set(ask_bits_, li + 1, window_);
      else
        best_bid_ = prev_set(bid_bits_, li - 1);
    }
  }
  return req.qty - left;
}

void OrderBook::rest(OrderId id, const OrderRequest& req, Qty qty) {
  const std::uint32_t idx = free_head_;
  Order& o = pool_[idx];
  free_head_ = o.next;

  o.id = id;
  o.price = req.price;
  o.qty = qty;
  o.owner = req.owner;
  o.side = req.side;
  o.next = kNil;

  const auto li = static_cast<std::uint32_t>(req.price - base_);
  Level& lv = levels_[li];
  o.prev = lv.tail;
  if (lv.tail != kNil)
    pool_[lv.tail].next = idx;
  else
    lv.head = idx;
  lv.tail = idx;
  lv.total += qty;
  ++lv.count;

  set_bit(req.side, li);
  if (req.side == Side::Buy)
    best_bid_ = std::max<std::int64_t>(best_bid_, li);
  else
    best_ask_ = std::min<std::int64_t>(best_ask_, li);

  ids_.insert(id, idx);
  listener_->on_level(req.side, req.price, lv.total);
}

bool OrderBook::cancel(OrderId id, CancelReason reason) {
  const std::uint32_t idx = ids_.find(id);
  if (idx == OrderIdMap::kMissing) return false;
  const Order o = pool_[idx];  // copy: the slot is recycled below
  const auto li = static_cast<std::uint32_t>(o.price - base_);

  unlink(idx);
  ids_.erase(id);
  release(idx);
  listener_->on_cancel(id, o.owner, o.side, o.price, o.qty, reason);
  listener_->on_level(o.side, o.price, levels_[li].total);

  if (levels_[li].count == 0) {
    clear_bit(o.side, li);
    if (o.side == Side::Buy && li == best_bid_) best_bid_ = prev_set(bid_bits_, static_cast<std::int64_t>(li) - 1);
    if (o.side == Side::Sell && li == best_ask_) best_ask_ = next_set(ask_bits_, li + 1, window_);
  }
  return true;
}

std::uint32_t OrderBook::cancel_all(ParticipantId owner, CancelReason reason) {
  // Walk only occupied levels (via the bitsets) instead of the whole pool.
  // Collect first: cancelling while iterating would mutate the lists.
  std::vector<OrderId>& victims = scratch_;
  victims.clear();
  for (const auto* bits : {&bid_bits_, &ask_bits_}) {
    for (std::int64_t i = next_set(*bits, 0, window_); i < window_; i = next_set(*bits, i + 1, window_)) {
      for (std::uint32_t idx = levels_[static_cast<std::size_t>(i)].head; idx != kNil; idx = pool_[idx].next)
        if (pool_[idx].owner == owner) victims.push_back(pool_[idx].id);
    }
  }
  for (OrderId id : victims) cancel(id, reason);
  return static_cast<std::uint32_t>(victims.size());
}

void OrderBook::unlink(std::uint32_t idx) {
  Order& o = pool_[idx];
  Level& lv = levels_[static_cast<std::size_t>(o.price - base_)];
  if (o.prev != kNil)
    pool_[o.prev].next = o.next;
  else
    lv.head = o.next;
  if (o.next != kNil)
    pool_[o.next].prev = o.prev;
  else
    lv.tail = o.prev;
  lv.total -= o.qty;
  --lv.count;
}

void OrderBook::release(std::uint32_t idx) {
  Order& o = pool_[idx];
  o.id = kNoOrder;
  o.qty = 0;
  o.prev = kNil;
  o.next = free_head_;
  free_head_ = idx;
}

Qty OrderBook::qty_at(Price p) const {
  return in_window(p) ? levels_[static_cast<std::size_t>(p - base_)].total : 0;
}

std::uint32_t OrderBook::depth(Side side, LevelView* out, std::uint32_t max_levels) const {
  std::uint32_t n = 0;
  if (side == Side::Buy) {
    for (std::int64_t i = best_bid_; i >= 0 && n < max_levels; i = prev_set(bid_bits_, i - 1)) {
      const Level& lv = levels_[static_cast<std::size_t>(i)];
      out[n++] = {base_ + i, lv.total, lv.count};
    }
  } else {
    for (std::int64_t i = best_ask_; i < window_ && n < max_levels; i = next_set(ask_bits_, i + 1, window_)) {
      const Level& lv = levels_[static_cast<std::size_t>(i)];
      out[n++] = {base_ + i, lv.total, lv.count};
    }
  }
  return n;
}

const Order* OrderBook::find(OrderId id) const {
  const std::uint32_t idx = ids_.find(id);
  return idx == OrderIdMap::kMissing ? nullptr : &pool_[idx];
}

bool OrderBook::ensure_in_window(Price p) {
  if (in_window(p)) return true;
  Price lo = p, hi = p;
  const std::int64_t lowest_bid = next_set(bid_bits_, 0, window_);
  const std::int64_t lowest_ask = best_ask_;
  const std::int64_t highest_ask = prev_set(ask_bits_, window_ - 1);
  const std::int64_t highest_bid = best_bid_;
  if (lowest_bid < window_) lo = std::min(lo, base_ + lowest_bid);
  if (lowest_ask < window_) lo = std::min(lo, base_ + lowest_ask);
  if (highest_ask >= 0) hi = std::max(hi, base_ + highest_ask);
  if (highest_bid >= 0) hi = std::max(hi, base_ + highest_bid);
  if (hi - lo + 1 > static_cast<Price>(window_)) return false;
  recenter(lo, hi);
  return true;
}

// Slides the price window so [lo, hi] sits in the middle. Rare (prices would
// have to drift thousands of ticks), so it is allowed to allocate.
void OrderBook::recenter(Price lo, Price hi) {
  const auto w = static_cast<Price>(window_);
  Price nb = (lo + hi) / 2 - w / 2;
  nb = std::min(nb, lo);
  nb = std::max(nb, hi - w + 1);

  std::vector<Level> moved(window_);
  std::fill(bid_bits_.begin(), bid_bits_.end(), 0);
  std::fill(ask_bits_.begin(), ask_bits_.end(), 0);
  for (std::uint32_t i = 0; i < window_; ++i) {
    if (levels_[i].count == 0) continue;
    const auto ni = static_cast<std::uint32_t>(base_ + i - nb);
    moved[ni] = levels_[i];
    set_bit(pool_[levels_[i].head].side, ni);
  }
  levels_.swap(moved);
  base_ = nb;
  best_bid_ = prev_set(bid_bits_, window_ - 1);
  best_ask_ = next_set(ask_bits_, 0, window_);
}

void OrderBook::set_bit(Side s, std::uint32_t i) {
  (s == Side::Buy ? bid_bits_ : ask_bits_)[i >> 6] |= 1ull << (i & 63);
}

void OrderBook::clear_bit(Side s, std::uint32_t i) {
  (s == Side::Buy ? bid_bits_ : ask_bits_)[i >> 6] &= ~(1ull << (i & 63));
}

}  // namespace btb
