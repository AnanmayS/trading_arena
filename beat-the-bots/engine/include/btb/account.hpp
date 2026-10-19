// Per-participant position and P&L accounting, all in integers.
//
// Instead of storing a (fractional) average price, an account keeps:
//   position   signed contracts
//   cash_ticks sum of -qty*price over every fill (what was paid/received, in ticks)
//   open_cost  sum of qty*price attributable to the open position
//   fees       cents paid
//
// Total P&L at a mark price is exact:  (cash_ticks + position*mark) * tick_value - fees.
// The realised/unrealised split uses open_cost; any integer rounding in that
// split moves cents between the two buckets but never changes the total.
// Because every fill adds -q*x to the buyer and +q*x to the seller, the sum of
// cash_ticks across all accounts is always zero, which is what makes the
// market provably zero-sum (net of fees).
#pragma once

#include <cstdlib>

#include "btb/contract.hpp"
#include "btb/types.hpp"

namespace btb {

struct Account {
  Qty position = 0;
  std::int64_t cash_ticks = 0;
  std::int64_t open_cost = 0;
  Cents fees = 0;

  Qty open_buy = 0;   // resting buy quantity (for worst-case exposure)
  Qty open_sell = 0;  // resting sell quantity
  std::uint32_t fills = 0;
  std::uint32_t volume = 0;
  std::uint32_t closes = 0;          // fills that reduced the position
  std::uint32_t winning_closes = 0;  // ... at a gross profit
  std::uint32_t liquidations = 0;
  bool risk_checked = false;  // pre-trade checks + liquidation apply
  bool liquidating = false;

  // Applies one fill of signed quantity q (positive = bought) at price x.
  void apply_fill(Qty q, Price x, Cents fee_per_contract) {
    const Qty pos = position;
    if (pos == 0 || (pos > 0) == (q > 0)) {
      open_cost += static_cast<std::int64_t>(q) * x;  // opening or adding
    } else if (std::abs(q) <= std::abs(pos)) {
      // Reducing: release the closed fraction of the cost basis.
      const std::int64_t released = open_cost * std::abs(q) / std::abs(pos);
      record_close(-static_cast<std::int64_t>(q) * x - released);
      open_cost -= released;
    } else {
      // Flipping through zero: close everything, the new position is at x.
      record_close(static_cast<std::int64_t>(pos) * x - open_cost);
      open_cost = static_cast<std::int64_t>(pos + q) * x;
    }
    position = pos + q;
    cash_ticks -= static_cast<std::int64_t>(q) * x;
    fees += fee_per_contract * std::abs(q);
    ++fills;
    volume += static_cast<std::uint32_t>(std::abs(q));
  }

  void record_close(std::int64_t gross_ticks) {
    ++closes;
    if (gross_ticks > 0) ++winning_closes;
  }

  // Cash-settles the open position at `price`; no fee.
  void settle(Price price) {
    if (position != 0) record_close(static_cast<std::int64_t>(position) * price - open_cost);
    cash_ticks += static_cast<std::int64_t>(position) * price;
    position = 0;
    open_cost = 0;
  }

  Cents total_pnl(Price mark, Cents tick_value) const {
    return (cash_ticks + static_cast<std::int64_t>(position) * mark) * tick_value - fees;
  }
  Cents unrealized(Price mark, Cents tick_value) const {
    return (static_cast<std::int64_t>(position) * mark - open_cost) * tick_value;
  }
  Cents realized(Cents tick_value) const { return (cash_ticks + open_cost) * tick_value - fees; }
};

}  // namespace btb
