// Open-addressing hash map from OrderId to a 32-bit pool index.
//
// Linear probing with backward-shift deletion: no tombstones, so lookups never
// degrade after millions of insert/erase cycles. Capacity is a power of two and
// the table grows at 50% load; size it up front and the hot path never
// allocates.
#pragma once

#include <cstdint>
#include <vector>

#include "btb/types.hpp"

namespace btb {

class OrderIdMap {
 public:
  static constexpr std::uint32_t kMissing = 0xFFFFFFFFu;

  explicit OrderIdMap(std::uint32_t expected = 1024) { rehash(capacity_for(expected)); }

  std::uint32_t find(OrderId key) const {
    for (std::uint32_t i = slot(key);; i = (i + 1) & mask_) {
      const Entry& e = table_[i];
      if (e.key == key) return e.value;
      if (e.key == kNoOrder) return kMissing;
    }
  }

  void insert(OrderId key, std::uint32_t value) {
    if ((size_ + 1) * 2 > table_.size()) rehash(static_cast<std::uint32_t>(table_.size() * 2));
    for (std::uint32_t i = slot(key);; i = (i + 1) & mask_) {
      Entry& e = table_[i];
      if (e.key == kNoOrder || e.key == key) {
        if (e.key == kNoOrder) ++size_;
        e = {key, value};
        return;
      }
    }
  }

  bool erase(OrderId key) {
    std::uint32_t i = slot(key);
    while (table_[i].key != key) {
      if (table_[i].key == kNoOrder) return false;
      i = (i + 1) & mask_;
    }
    // Backward-shift: pull later entries of the same probe run into the hole.
    std::uint32_t hole = i;
    for (std::uint32_t j = (i + 1) & mask_; table_[j].key != kNoOrder; j = (j + 1) & mask_) {
      const std::uint32_t home = slot(table_[j].key);
      // Move j into the hole iff its home slot is not cyclically in (hole, j].
      const bool home_after_hole = ((j - home) & mask_) >= ((j - hole) & mask_);
      if (home_after_hole) {
        table_[hole] = table_[j];
        hole = j;
      }
    }
    table_[hole] = {};
    --size_;
    return true;
  }

  std::uint32_t size() const { return size_; }
  void clear() {
    for (auto& e : table_) e = {};
    size_ = 0;
  }

 private:
  struct Entry {
    OrderId key = kNoOrder;
    std::uint32_t value = 0;
  };

  static std::uint32_t capacity_for(std::uint32_t expected) {
    std::uint32_t c = 16;
    while (c < expected * 2) c <<= 1;
    return c;
  }

  // Fibonacci hashing: order ids are sequential, so spread them with a
  // multiplicative hash and take the high bits.
  std::uint32_t slot(OrderId key) const {
    return static_cast<std::uint32_t>((key * 0x9E3779B97F4A7C15ull) >> shift_);
  }

  void rehash(std::uint32_t capacity) {
    std::vector<Entry> old;
    old.swap(table_);
    table_.assign(capacity, Entry{});
    mask_ = capacity - 1;
    shift_ = 64;
    for (std::uint32_t c = capacity; c > 1; c >>= 1) --shift_;
    size_ = 0;
    for (const Entry& e : old)
      if (e.key != kNoOrder) insert(e.key, e.value);
  }

  std::vector<Entry> table_;
  std::uint32_t mask_ = 0;
  std::uint32_t shift_ = 64;
  std::uint32_t size_ = 0;
};

}  // namespace btb
