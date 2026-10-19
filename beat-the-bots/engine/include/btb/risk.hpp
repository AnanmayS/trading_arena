// Pre-trade risk checks and margin rules for risk-checked participants (the
// player). Stateless: everything it needs is passed in.
#pragma once

#include <algorithm>
#include <cstdlib>

#include "btb/account.hpp"
#include "btb/contract.hpp"
#include "btb/order_book.hpp"

namespace btb {

class RiskEngine {
 public:
  explicit RiskEngine(const ContractSpec& spec) : spec_(spec) {}

  // Worst-case absolute position if every resting order on one side filled.
  static Qty worst_exposure(const Account& a, Qty extra_buy = 0, Qty extra_sell = 0) {
    const Qty if_buys_fill = a.position + a.open_buy + extra_buy;
    const Qty if_sells_fill = a.position - a.open_sell - extra_sell;
    return std::max(std::abs(if_buys_fill), std::abs(if_sells_fill));
  }

  // Orders that reduce worst-case exposure are always allowed, so a player who
  // is over-margined can still get out.
  RejectReason pre_trade(const Account& a, const OrderRequest& req, Price mark, Cents equity) const {
    if (a.liquidating) return RejectReason::Liquidating;
    if (req.qty <= 0) return RejectReason::InvalidQty;
    if (req.type != OrderType::Market && std::abs(req.price - mark) > spec_.price_band)
      return RejectReason::PriceBand;

    const Qty before = worst_exposure(a);
    const Qty after = req.side == Side::Buy ? worst_exposure(a, req.qty, 0) : worst_exposure(a, 0, req.qty);
    if (after > before) {
      if (after > spec_.max_position) return RejectReason::MaxPosition;
      if (static_cast<Cents>(after) * spec_.initial_margin > equity) return RejectReason::InsufficientMargin;
    }
    return RejectReason::None;
  }

  bool breaches_maintenance(const Account& a, Cents equity) const {
    return a.position != 0 && equity < maintenance_requirement(a);
  }

  Cents initial_requirement(const Account& a) const {
    return static_cast<Cents>(std::abs(a.position)) * spec_.initial_margin;
  }
  Cents maintenance_requirement(const Account& a) const {
    return static_cast<Cents>(std::abs(a.position)) * spec_.maintenance_margin;
  }

  // How many ticks the market can move against the position before
  // liquidation; -1 when flat.
  std::int64_t ticks_to_liquidation(const Account& a, Cents equity) const {
    if (a.position == 0) return -1;
    const Cents excess = equity - maintenance_requirement(a);
    if (excess <= 0) return 0;
    return excess / (static_cast<Cents>(std::abs(a.position)) * spec_.tick_value);
  }

  const ContractSpec& spec() const { return spec_; }

 private:
  ContractSpec spec_;
};

}  // namespace btb
