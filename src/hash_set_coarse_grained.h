#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <cassert>
#include <vector>
#include <functional>
#include <algorithm>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(size_t /*initial_capacity*/) {}

  bool Add(T /*elem*/) final {
    assert(false && "Not implemented yet");
    return false;
  }

  bool Remove(T /*elem*/) final {
    assert(false && "Not implemented yet");
    return false;
  }

  [[nodiscard]] bool Contains(T /*elem*/) final {
    assert(false && "Not implemented yet");
    return false;
  }

  [[nodiscard]] size_t Size() const final {
    assert(false && "Not implemented yet");
    return 0u;
  }
 private:
  std::vector<std::vector<T>> table_;
  size_t set_size_; //tracks the number of elements in the table
  std::hash<T> hasher_;
};

#endif  // HASH_SET_COARSE_GRAINED_H
