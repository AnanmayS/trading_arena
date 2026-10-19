#include "btb/types.hpp"

namespace btb {

const char* to_string(RejectReason r) {
  switch (r) {
    case RejectReason::None: return "none";
    case RejectReason::InvalidQty: return "invalid quantity";
    case RejectReason::InvalidPrice: return "invalid price";
    case RejectReason::PriceOutOfRange: return "price out of range";
    case RejectReason::PoolExhausted: return "order pool exhausted";
    case RejectReason::MaxPosition: return "max position exceeded";
    case RejectReason::InsufficientMargin: return "insufficient margin";
    case RejectReason::PriceBand: return "price outside band";
    case RejectReason::RoundClosed: return "round closed";
    case RejectReason::UnknownOrder: return "unknown order";
    case RejectReason::Liquidating: return "account liquidating";
    case RejectReason::NoLiquidity: return "no liquidity";
  }
  return "?";
}

const char* to_string(CancelReason r) {
  switch (r) {
    case CancelReason::User: return "user";
    case CancelReason::SelfTrade: return "self-trade prevention";
    case CancelReason::Unfilled: return "unfilled remainder";
    case CancelReason::Risk: return "risk";
    case CancelReason::Expiry: return "expiry";
  }
  return "?";
}

}  // namespace btb
