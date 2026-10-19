// Contract specification for BTB, modelled on the E-mini S&P 500.
#pragma once

#include "btb/types.hpp"

namespace btb {

struct ContractSpec {
  // 0.25 index points per tick, $50 per point => $12.50 per tick.
  Price ticks_per_point = 4;
  Cents tick_value = 1250;

  Cents fee_per_contract = 100;        // charged on every fill, each side
  Cents initial_margin = 600'000;      // per contract, checked pre-trade
  Cents maintenance_margin = 500'000;  // per contract, liquidation threshold
  Cents starting_cash = 2'500'000;     // $25,000
  Qty max_position = 10;

  // Fat-finger band: player limit prices more than this far from the mark are
  // rejected, and player market orders cannot trade beyond it (market
  // protection, like an exchange's protection points).
  Price price_band = 200;  // 50 points
};

}  // namespace btb
