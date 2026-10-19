// Hidden "true value" of the contract: a random walk with mean-reverting
// stochastic volatility and news-driven jumps.
//
// All state is 16.16 fixed point in ticks, so the walk is bit-identical on
// every platform (no libm, no FMA contraction differences between native and
// WebAssembly).
#pragma once

#include <cstdint>

#include "btb/config.hpp"
#include "btb/rng.hpp"

namespace btb {

class FairValue {
 public:
  FairValue(Price initial, std::int64_t vol_fp, std::uint32_t tick_ms, std::uint64_t seed);

  // Advances one tick. `regime_fp` scales the long-run volatility target
  // (calm open, hot close, ...).
  void step(std::int64_t regime_fp);
  void jump(Price ticks) { fv_ += ticks * kFp; }

  Price value() const { return (fv_ + kFp / 2) >> 16; }  // nearest tick
  std::int64_t raw_fp() const { return fv_; }
  std::int64_t vol_fp() const { return vol_; }

 private:
  std::int64_t fv_;
  std::int64_t vol_;
  std::int64_t base_vol_;
  std::int64_t sqrt_dt_fp_;
  Rng rng_;
};

std::uint64_t isqrt(std::uint64_t x);

}  // namespace btb
