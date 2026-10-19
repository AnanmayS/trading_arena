// Mirrors engine/include/btb/snapshot.hpp and engine/include/btb/event.hpp.
// These are an ABI: keep them in lock-step with the C++ headers (the engine
// writes FRAME_VERSION into slot 0 and the wrapper refuses a mismatch).

export const FRAME_VERSION = 1;
export const LEVELS = 20;
export const MAX_ORDERS = 32;

export const Slot = {
  Version: 0,
  TimeUs: 1,
  RoundUs: 2,
  Phase: 3,
  Finished: 4,
  BestBid: 5,
  BestAsk: 6,
  LastTrade: 7,
  Mark: 8,
  Position: 9,
  OpenCost: 10,
  Realized: 11,
  Unrealized: 12,
  Equity: 13,
  InitialMargin: 14,
  MaintMargin: 15,
  TicksToLiq: 16,
  OpenBuy: 17,
  OpenSell: 18,
  Liquidating: 19,
  Fees: 20,
  Fills: 21,
  BidCount: 22,
  AskCount: 23,
  OrderCount: 24,
  TotalPnl: 25,
  MaxDrawdown: 26,
  StartingCash: 27,
  TickValue: 28,
  TicksPerPoint: 29,
  MaxPosition: 30,
  PriceBand: 31,
  Liquidations: 32,
} as const;

export const HEADER_SIZE = 48;
export const BIDS_OFFSET = HEADER_SIZE;
export const ASKS_OFFSET = BIDS_OFFSET + LEVELS * 3;
export const ORDERS_OFFSET = ASKS_OFFSET + LEVELS * 3;
export const FRAME_SIZE = ORDERS_OFFSET + MAX_ORDERS * 4;

// Event: 48-byte little-endian struct.
export const EVENT_SIZE = 48;
export const EventField = {
  timeUs: 0, // i64
  price: 8, // i64
  value: 16, // i64
  orderId: 24, // u64
  qty: 32, // i32
  participant: 36, // u32
  type: 40, // u8
  side: 41, // u8
  reason: 42, // u8
  flags: 43, // u8
  aux: 44, // u32
} as const;

export const EventType = {
  Ack: 1,
  Fill: 2,
  Cancel: 3,
  Reject: 4,
  Trade: 5,
  BookDelta: 6,
  MarginCall: 7,
  Liquidation: 8,
  News: 9,
  Settlement: 10,
} as const;
export type EventTypeCode = (typeof EventType)[keyof typeof EventType];

export const Side = { Buy: 0, Sell: 1 } as const;
export type SideCode = (typeof Side)[keyof typeof Side];

export const OrderType = { Limit: 0, Market: 1, IOC: 2 } as const;
export type OrderTypeCode = (typeof OrderType)[keyof typeof OrderType];

export const Phase = { Open: 0, Midday: 1, Close: 2, Settlement: 3, Settled: 4 } as const;
export type PhaseCode = (typeof Phase)[keyof typeof Phase];

export const PLAYER = 0;
