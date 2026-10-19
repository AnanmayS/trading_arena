// Latency percentiles for add / cancel / match / mixed.
//
// Apple Silicon's steady clock ticks every ~41.7 ns, which is coarser than a
// single book operation, so ops are timed in batches of 32 and the per-op
// latency of each batch is recorded. p99 therefore reflects the slowest
// batches (multi-level sweeps, cache misses), not single-op outliers.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "btb/order_book.hpp"
#include "btb/rng.hpp"
#include "workload.hpp"

using namespace btb;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kBatch = 32;
constexpr int kSamples = 20000;  // batches per scenario

struct Result {
  const char* name;
  double mean, p50, p99, mops;
};

template <class Setup, class Body>
Result measure(const char* name, Setup setup, Body body) {
  std::vector<double> per_op;
  per_op.reserve(kSamples);
  double total_ns = 0;
  for (int s = 0; s < kSamples; ++s) {
    setup(s);
    const auto t0 = Clock::now();
    for (int k = 0; k < kBatch; ++k) body(s, k);
    const auto t1 = Clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kBatch;
    per_op.push_back(ns);
    total_ns += ns;
  }
  std::sort(per_op.begin(), per_op.end());
  const double mean = total_ns / kSamples;
  return {name, mean, per_op[per_op.size() / 2], per_op[per_op.size() * 99 / 100], 1e3 / mean};
}

}  // namespace

int main() {
  const BookConfig cfg{20000, 8192, 1u << 20};
  std::vector<Result> results;
  Rng rng(11);

  {
    OrderBook book(cfg);
    std::vector<Price> px(kBatch);
    results.push_back(measure(
        "add (passive limit)",
        [&](int s) {
          if (s % 64 == 0) book.cancel_all(1);
          for (auto& p : px) p = 20000 - rng.range(1, 50);
        },
        [&](int, int k) { book.submit({1, Side::Buy, OrderType::Limit, px[k], 1}); }));
  }
  {
    OrderBook book(cfg);
    std::vector<OrderId> ids(kBatch);
    results.push_back(measure(
        "cancel",
        [&](int) {
          for (auto& id : ids) id = book.submit({1, Side::Sell, OrderType::Limit, 20000 + rng.range(1, 50), 1}).id;
          for (int i = kBatch - 1; i > 0; --i) std::swap(ids[i], ids[rng.below(i + 1)]);
        },
        [&](int, int k) { book.cancel(ids[k]); }));
  }
  {
    OrderBook book(cfg);
    results.push_back(measure(
        "match (1 level)",
        [&](int) {
          for (int i = 0; i < kBatch; ++i) book.submit({1, Side::Sell, OrderType::Limit, 20000 + rng.range(1, 50), 1});
        },
        [&](int, int) { book.submit({2, Side::Buy, OrderType::Market, 0, 1}); }));
  }
  {
    OrderBook book(cfg);
    const auto ops = bench::mixed_workload(static_cast<std::size_t>(kSamples) * kBatch, 5);
    std::vector<OrderId> live;
    live.reserve(ops.size());
    results.push_back(measure(
        "mixed (55/35/10)", [](int) {},
        [&](int s, int k) { bench::apply(book, ops[static_cast<std::size_t>(s) * kBatch + k], live); }));
  }

  std::printf("| operation | mean ns/op | p50 ns/op | p99 ns/op | M ops/s |\n");
  std::printf("|---|---:|---:|---:|---:|\n");
  for (const auto& r : results)
    std::printf("| %s | %.1f | %.1f | %.1f | %.1f |\n", r.name, r.mean, r.p50, r.p99, r.mops);
  return 0;
}
