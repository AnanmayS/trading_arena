// Throughput benchmarks for the order book: add, cancel, match and a mixed
// steady-state workload. Run: ./btb_bench --benchmark_counters_tabular=true
#include <benchmark/benchmark.h>

#include "btb/order_book.hpp"
#include "btb/rng.hpp"
#include "workload.hpp"

using namespace btb;

namespace {

constexpr int kBatch = 4096;
const BookConfig kCfg{20000, 8192, 1u << 20};

std::vector<Price> passive_prices(Side side, std::uint64_t seed) {
  Rng rng(seed);
  std::vector<Price> px(kBatch);
  for (auto& p : px) p = side == Side::Buy ? 20000 - rng.range(1, 50) : 20000 + rng.range(1, 50);
  return px;
}

// Adding a non-crossing limit order: pool pop, FIFO append, bitset set, map insert.
void BM_AddLimit(benchmark::State& state) {
  OrderBook book(kCfg);
  const auto px = passive_prices(Side::Buy, 1);
  for (auto _ : state) {
    for (int i = 0; i < kBatch; ++i)
      benchmark::DoNotOptimize(book.submit({1, Side::Buy, OrderType::Limit, px[i], 1}));
    state.PauseTiming();
    book.cancel_all(1);
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_AddLimit);

// Cancelling a resting order by id, in random queue positions.
void BM_Cancel(benchmark::State& state) {
  OrderBook book(kCfg);
  const auto px = passive_prices(Side::Sell, 2);
  std::vector<OrderId> ids(kBatch);
  Rng rng(3);
  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < kBatch; ++i) ids[i] = book.submit({1, Side::Sell, OrderType::Limit, px[i], 1}).id;
    for (int i = kBatch - 1; i > 0; --i) std::swap(ids[i], ids[rng.below(static_cast<std::uint32_t>(i + 1))]);
    state.ResumeTiming();
    for (OrderId id : ids) benchmark::DoNotOptimize(book.cancel(id));
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_Cancel);

// An aggressive order that fully fills against one resting order.
void BM_MatchSingle(benchmark::State& state) {
  OrderBook book(kCfg);
  const auto px = passive_prices(Side::Sell, 4);
  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < kBatch; ++i) book.submit({1, Side::Sell, OrderType::Limit, px[i], 1});
    state.ResumeTiming();
    for (int i = 0; i < kBatch; ++i) benchmark::DoNotOptimize(book.submit({2, Side::Buy, OrderType::Market, 0, 1}));
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_MatchSingle);

// A market order that sweeps 10 price levels.
void BM_MatchSweep10(benchmark::State& state) {
  OrderBook book(kCfg);
  constexpr int kSweeps = 256;
  for (auto _ : state) {
    state.PauseTiming();
    for (int s = 0; s < kSweeps; ++s)
      for (Price p = 20001; p <= 20010; ++p) book.submit({1, Side::Sell, OrderType::Limit, p, 1});
    state.ResumeTiming();
    for (int s = 0; s < kSweeps; ++s) benchmark::DoNotOptimize(book.submit({2, Side::Buy, OrderType::Market, 0, 10}));
  }
  state.SetItemsProcessed(state.iterations() * kSweeps);
}
BENCHMARK(BM_MatchSweep10);

// Steady state: 55% passive adds, 35% cancels, 10% market orders.
void BM_Mixed(benchmark::State& state) {
  OrderBook book(kCfg);
  const auto ops = bench::mixed_workload(1 << 20, 5);
  std::vector<OrderId> live;
  live.reserve(1 << 20);
  std::size_t i = 0;
  for (auto _ : state) {
    bench::apply(book, ops[i], live);
    if (++i == ops.size()) i = 0;
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Mixed);

}  // namespace

BENCHMARK_MAIN();
