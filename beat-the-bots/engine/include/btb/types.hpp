// Core vocabulary types shared by every engine component.
//
// Everything that touches prices, quantities or money is an integer. Prices are
// expressed in ticks, money in cents. Converting to points or dollars happens
// only at the display edge (the TypeScript layer).
#pragma once

#include <cstdint>

namespace btb {

using Price = std::int64_t;          // price in ticks
using Qty = std::int32_t;            // contracts
using Cents = std::int64_t;          // money, 1 = $0.01
using OrderId = std::uint64_t;       // 0 is never a valid id
using ParticipantId = std::uint32_t;
using SimTime = std::int64_t;        // simulation time in microseconds

inline constexpr OrderId kNoOrder = 0;
inline constexpr ParticipantId kPlayer = 0;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }
constexpr Qty signed_qty(Side s, Qty q) { return s == Side::Buy ? q : -q; }

enum class OrderType : std::uint8_t {
  Limit = 0,   // rests whatever does not fill immediately
  Market = 1,  // sweeps the book, remainder is cancelled
  IOC = 2,     // limit price, remainder is cancelled
  Stop = 3,    // stop-market: price is the trigger. Held by the Exchange, never
               //   rests in the book; becomes a market order when the last
               //   trade reaches the trigger (see Exchange::check_stops)
};

enum class CancelReason : std::uint8_t {
  User = 0,       // explicit cancel / cancel-all
  SelfTrade = 1,  // resting order removed by self-trade prevention
  Unfilled = 2,   // remainder of a market / IOC order
  Risk = 3,       // pulled by the risk engine (margin call)
  Expiry = 4,     // round ended
};

enum class RejectReason : std::uint8_t {
  None = 0,
  InvalidQty = 1,
  InvalidPrice = 2,
  PriceOutOfRange = 3,  // outside the book's addressable price window
  PoolExhausted = 4,
  MaxPosition = 5,
  InsufficientMargin = 6,
  PriceBand = 7,        // fat-finger protection
  RoundClosed = 8,
  UnknownOrder = 9,
  Liquidating = 10,
  NoLiquidity = 11,
  StopPrice = 12,       // stop trigger already reached by the last trade
  TooManyStops = 13,
};

const char* to_string(RejectReason r);
const char* to_string(CancelReason r);

}  // namespace btb
