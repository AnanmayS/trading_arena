// Deterministic random numbers: xoshiro256** seeded through splitmix64.
//
// Only integer arithmetic is used so native and WebAssembly builds produce the
// same stream bit for bit. The "normal" sampler is an Irwin-Hall sum of twelve
// 16-bit uniforms, which is plenty for a market simulation and avoids libm.
#pragma once

#include <cstdint>

namespace btb {

class Rng {
 public:
  explicit Rng(std::uint64_t seed = 0x5EEDull) { reseed(seed); }

  void reseed(std::uint64_t seed) {
    std::uint64_t x = seed;
    for (auto& word : s_) word = splitmix64(x);
  }

  std::uint64_t next() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform integer in [0, n). Lemire's multiply-shift; the tiny bias for
  // huge n is irrelevant here and keeps it branch-free.
  std::uint32_t below(std::uint32_t n) {
    return static_cast<std::uint32_t>(((next() >> 32) * static_cast<std::uint64_t>(n)) >> 32);
  }

  // Uniform integer in [lo, hi].
  std::int64_t range(std::int64_t lo, std::int64_t hi) {
    return lo + static_cast<std::int64_t>(below(static_cast<std::uint32_t>(hi - lo + 1)));
  }

  // True with probability num / den.
  bool chance(std::uint32_t num, std::uint32_t den) { return below(den) < num; }

  // Approximately N(0, 1) scaled by 65536 (16.16 fixed point), in about
  // [-6, 6] standard deviations.
  std::int64_t normal_fp16() {
    std::int64_t sum = 0;
    for (int i = 0; i < 3; ++i) {
      const std::uint64_t r = next();
      sum += static_cast<std::int64_t>(r & 0xFFFF) + static_cast<std::int64_t>((r >> 16) & 0xFFFF) +
             static_cast<std::int64_t>((r >> 32) & 0xFFFF) + static_cast<std::int64_t>(r >> 48);
    }
    return sum - 6 * 65536;
  }

 private:
  static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  static std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  std::uint64_t s_[4]{};
};

}  // namespace btb
