// Shared synthetic order flow for the benchmarks. Ops are generated up front so
// the timed loops measure the book, not the random number generator.
#pragma once

#include <vector>

#include "btb/order_book.hpp"
#include "btb/rng.hpp"

namespace btb::bench {

struct Op {
  enum Kind : std::uint8_t { Add, Cancel, Market } kind;
  OrderRequest req;
  std::uint32_t cancel_slot;  // index into the live-order list for cancels
};

// Steady-state mix resembling the game's bots: mostly passive limits near the
// touch, frequent cancels, some aggressive orders.
inline std::vector<Op> mixed_workload(std::size_t n, std::uint64_t seed) {
  Rng rng(seed);
  std::vector<Op> ops;
  ops.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto r = rng.below(100);
    const Side side = rng.chance(1, 2) ? Side::Buy : Side::Sell;
    const auto owner = static_cast<ParticipantId>(1 + rng.below(32));
    const Qty qty = static_cast<Qty>(rng.range(1, 5));
    if (r < 55) {
      // Passive: buy below mid, sell above mid, 1-20 ticks away.
      const Price off = rng.range(1, 20);
      const Price px = side == Side::Buy ? 20000 - off : 20000 + off;
      ops.push_back({Op::Add, {owner, side, OrderType::Limit, px, qty}, 0});
    } else if (r < 90) {
      ops.push_back({Op::Cancel, {}, rng.below(1u << 30)});
    } else {
      ops.push_back({Op::Market, {owner, side, OrderType::Market, 0, qty}, 0});
    }
  }
  return ops;
}

// Executes one op, maintaining the list of possibly-live ids for cancels.
inline void apply(OrderBook& book, const Op& op, std::vector<OrderId>& live) {
  switch (op.kind) {
    case Op::Add: {
      auto r = book.submit(op.req);
      if (r.resting > 0) live.push_back(r.id);
      break;
    }
    case Op::Cancel:
      if (!live.empty()) {
        const std::size_t i = op.cancel_slot % live.size();
        book.cancel(live[i]);
        live[i] = live.back();
        live.pop_back();
      }
      break;
    case Op::Market:
      book.submit(op.req);
      break;
  }
}

}  // namespace btb::bench
