#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

#include "btb/flat_map.hpp"
#include "btb/rng.hpp"

using namespace btb;

TEST_CASE("flat map basic insert / find / erase") {
  OrderIdMap m(4);
  CHECK(m.find(1) == OrderIdMap::kMissing);
  m.insert(1, 10);
  m.insert(2, 20);
  CHECK(m.find(1) == 10);
  CHECK(m.find(2) == 20);
  m.insert(1, 11);  // overwrite
  CHECK(m.find(1) == 11);
  CHECK(m.size() == 2);
  CHECK(m.erase(1));
  CHECK_FALSE(m.erase(1));
  CHECK(m.find(1) == OrderIdMap::kMissing);
  CHECK(m.find(2) == 20);
}

TEST_CASE("flat map matches std::unordered_map under random churn") {
  OrderIdMap m(16);  // forces several rehashes
  std::unordered_map<OrderId, std::uint32_t> ref;
  Rng rng(7);
  for (int i = 0; i < 200000; ++i) {
    const OrderId key = 1 + rng.below(5000);
    if (rng.chance(3, 5)) {
      const auto v = rng.below(1000000);
      m.insert(key, v);
      ref[key] = v;
    } else {
      CHECK(m.erase(key) == (ref.erase(key) == 1));
    }
  }
  REQUIRE(m.size() == ref.size());
  for (OrderId k = 1; k <= 5000; ++k) {
    auto it = ref.find(k);
    CHECK(m.find(k) == (it == ref.end() ? OrderIdMap::kMissing : it->second));
  }
}
