// Engine events and the ring buffer that carries them to the UI.
//
// Event is a fixed 48-byte POD with no padding. The TypeScript side reads it
// straight out of WebAssembly memory, so the layout here is mirrored in
// web/src/engine/layout.ts; the static_asserts below guard it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "btb/types.hpp"

namespace btb {

enum class EventType : std::uint8_t {
  Ack = 1,          // order accepted: order_id, participant, side, price, qty, reason=OrderType
  Fill = 2,         // one side of a trade: participant, order_id, side, price, qty,
                    //   value=position after fill, flags bit0=maker
  Cancel = 3,       // order removed: order_id, participant, side, price, qty=remaining, reason=CancelReason
  Reject = 4,       // order refused: participant, side, price, qty, reason=RejectReason
  Trade = 5,        // public print: price, qty, side=aggressor, order_id=taker order,
                    //   participant=taker, aux=maker participant
  BookDelta = 6,    // level total changed: side, price, qty=new total
  MarginCall = 7,   // participant, price=mark, qty=position, value=equity
  Liquidation = 8,  // participant, qty=position being closed
  News = 9,         // aux=headline id, value=sentiment (-3..3)
  Settlement = 10,  // participant, price=settlement price, qty=position settled, value=final P&L
};

struct Event {
  std::int64_t time_us = 0;      //  0
  std::int64_t price = 0;        //  8  ticks
  std::int64_t value = 0;        // 16  type-specific, money in cents
  std::uint64_t order_id = 0;    // 24
  std::int32_t qty = 0;          // 32
  std::uint32_t participant = 0; // 36
  EventType type{};              // 40
  Side side = Side::Buy;         // 41
  std::uint8_t reason = 0;       // 42
  std::uint8_t flags = 0;        // 43
  std::uint32_t aux = 0;         // 44
};

static_assert(sizeof(Event) == 48);
static_assert(std::is_trivially_copyable_v<Event>);
static_assert(std::has_unique_object_representations_v<Event>, "Event must have no padding");
static_assert(offsetof(Event, qty) == 32 && offsetof(Event, type) == 40 && offsetof(Event, aux) == 44);

// Single-producer ring buffer. The engine pushes during a step; the UI drains
// once per frame. If the consumer falls behind, the oldest events are dropped
// (and counted): events are a view of the market, never the source of truth.
//
// A running FNV-1a hash of every event ever pushed makes determinism checks
// trivial: same seed + same inputs => same hash, on any platform.
class EventRing {
 public:
  explicit EventRing(std::uint32_t capacity_pow2 = 1u << 16)
      : buf_(capacity_pow2), mask_(capacity_pow2 - 1) {}

  void push(const Event& e) {
    if (head_ - tail_ == buf_.size()) {
      ++tail_;
      ++dropped_;
    }
    buf_[head_ & mask_] = e;
    ++head_;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&e);
    for (std::size_t i = 0; i < sizeof(Event); ++i) hash_ = (hash_ ^ bytes[i]) * 0x100000001B3ull;
  }

  // Copies up to `max` pending events into `out`, oldest first.
  std::uint32_t drain(Event* out, std::uint32_t max) {
    std::uint32_t n = 0;
    while (tail_ != head_ && n < max) out[n++] = buf_[tail_++ & mask_];
    return n;
  }

  std::uint32_t pending() const { return static_cast<std::uint32_t>(head_ - tail_); }
  std::uint64_t total() const { return head_; }
  std::uint64_t dropped() const { return dropped_; }
  std::uint64_t hash() const { return hash_; }

 private:
  std::vector<Event> buf_;
  std::uint64_t mask_;
  std::uint64_t head_ = 0;
  std::uint64_t tail_ = 0;
  std::uint64_t dropped_ = 0;
  std::uint64_t hash_ = 0xCBF29CE484222325ull;
};

}  // namespace btb
