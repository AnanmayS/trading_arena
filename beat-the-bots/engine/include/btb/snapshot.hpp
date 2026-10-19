// Flat per-frame snapshot of everything the UI renders, written into a
// preallocated int64 buffer so the browser can read it through one typed-array
// view instead of making dozens of calls across the WASM boundary.
//
// The layout is an ABI shared with web/src/engine/layout.ts. Bump kVersion
// whenever it changes.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "btb/event.hpp"
#include "btb/market_sim.hpp"

namespace btb::snapshot {

inline constexpr std::int64_t kVersion = 2;
inline constexpr int kLevels = 20;     // book levels per side
inline constexpr int kMaxOrders = 32;  // player working orders
inline constexpr int kMaxStops = 16;   // player working stops
static_assert(kMaxStops == static_cast<int>(kMaxStopsPerParticipant));

// Header slots.
enum Slot : int {
  Version = 0,
  TimeUs,        // elapsed round time, >= 0
  RoundUs,
  PhaseSlot,
  Finished,
  BestBid,       // 0 when the side is empty
  BestAsk,
  LastTrade,     // 0 before the first trade
  Mark,
  Position,
  OpenCost,      // ticks*contracts; avg price = OpenCost / Position
  Realized,      // cents, net of fees
  Unrealized,    // cents
  Equity,        // cents
  InitialMargin, // cents in use
  MaintMargin,   // cents required
  TicksToLiq,    // -1 when flat
  OpenBuy,
  OpenSell,
  Liquidating,
  Fees,
  Fills,
  BidCount,
  AskCount,
  OrderCount,
  TotalPnl,
  MaxDrawdown,
  StartingCash,
  TickValue,
  TicksPerPoint,
  MaxPosition,
  PriceBand,
  Liquidations,
  StopCount,
  HeaderSize = 48,
};

inline constexpr int kBidsOffset = HeaderSize;                 // kLevels x (price, qty, orders)
inline constexpr int kAsksOffset = kBidsOffset + kLevels * 3;  // kLevels x (price, qty, orders)
inline constexpr int kOrdersOffset = kAsksOffset + kLevels * 3;  // kMaxOrders x (id, side, price, qty)
inline constexpr int kStopsOffset = kOrdersOffset + kMaxOrders * 4;  // kMaxStops x (id, side, trigger, qty)
inline constexpr int kFrameSize = kStopsOffset + kMaxStops * 4;

using Frame = std::array<std::int64_t, kFrameSize>;

void write(const MarketSim& sim, Frame& out);

// End-of-round results and per-bot P&L as a JSON object (called once).
std::string summary_json(const MarketSim& sim);
// Static catalog: headlines, scenarios, contract spec (called once).
std::string catalog_json();

// Plays a fixed scripted round and returns the final event-stream hash. Used
// to prove native and WebAssembly builds produce identical markets.
std::uint64_t self_test(std::uint64_t seed, const GameConfig& cfg);

}  // namespace btb::snapshot
