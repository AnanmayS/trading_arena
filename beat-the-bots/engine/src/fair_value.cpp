#include "btb/fair_value.hpp"

#include <algorithm>

namespace btb {

std::uint64_t isqrt(std::uint64_t x) {
  std::uint64_t r = 0;
  for (std::uint64_t bit = 1ull << 62; bit; bit >>= 2) {
    if (x >= r + bit) {
      x -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
  }
  return r;
}

FairValue::FairValue(Price initial, std::int64_t vol_fp, std::uint32_t tick_ms, std::uint64_t seed)
    : fv_(initial * kFp),
      vol_(vol_fp),
      base_vol_(vol_fp),
      // sqrt(dt) in 16.16 = sqrt(dt * 2^32), with dt in seconds.
      sqrt_dt_fp_(static_cast<std::int64_t>(isqrt((static_cast<std::uint64_t>(tick_ms) << 32) / 1000))),
      rng_(seed) {}

void FairValue::step(std::int64_t regime_fp) {
  const std::int64_t target = base_vol_ * regime_fp / kFp;
  // Volatility mean-reverts to the regime target (time constant ~128 ticks)
  // and wanders with 3% vol-of-vol per tick.
  vol_ += (target - vol_) / 128;
  vol_ += vol_ * rng_.normal_fp16() / kFp * 1966 / kFp;
  vol_ = std::clamp(vol_, target / 3, target * 3);

  const std::int64_t sigma_tick = vol_ * sqrt_dt_fp_ / kFp;  // ticks (fp) per tick
  fv_ += sigma_tick * rng_.normal_fp16() / kFp;
}

}  // namespace btb
