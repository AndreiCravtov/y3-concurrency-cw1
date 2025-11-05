#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <algorithm>
#include <cassert>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(const size_t capacity)
      : table_(capacity), set_size_(0) {
    assert(capacity > 0);
  }

  bool Add(T elem) final {
    {
      // scope-lock for mutual exclusion
      std::scoped_lock<std::mutex> lock(mutex_);

      auto& bucket = Bucket_(elem);

      // 3) return false on duplicate (loops over the elements in that bucket)
      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        return false;
      }

      // 4) insert and update size if not present
      bucket.push_back(std::move(elem));
      set_size_++;
    }  // release lock

    // 5) apply resizing policy if needed
    ResizeIfNeeded_();
    return true;
  }

  bool Remove(T elem) final {
    // scope-lock for mutual exclusion
    std::scoped_lock<std::mutex> lock(mutex_);

    auto& bucket = Bucket_(elem);

    // find element position (returning early if doesn't exist)
    auto i = std::find(bucket.begin(), bucket.end(), elem);
    if (i == bucket.end()) return false;

    // remove element & decrement size
    bucket.erase(i);
    set_size_--;
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    // scope-lock for mutual exclusion
    std::scoped_lock<std::mutex> lock(mutex_);

    auto& bucket = Bucket_(elem);

    // return if found or not
    auto found = std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
    return found;
  }

  [[nodiscard]] size_t Size() const final {
    // scope-lock for mutual exclusion
    std::scoped_lock<std::mutex> lock(mutex_);

    return set_size_;
  }

 private:
  std::vector<std::vector<T>> table_;
  size_t set_size_;  // tracks the number of elements in the table
  std::hash<T> hasher_;
  mutable std::mutex mutex_;

  /**
   * Returns the bucket associated with the element.
   */
  std::vector<T>& Bucket_(T elem) {
    return table_[hasher_(elem) % table_.size()];
  }

  bool Policy_() { return set_size_ / table_.size() > 4; }

  void ResizeIfNeeded_() {
    // scope-lock for mutual exclusion
    std::scoped_lock<std::mutex> lock(mutex_);

    // check if need to resize
    if (!Policy_()) return;

    // 1) create a new empty table with double the number of buckets
    std::vector<std::vector<T>> new_table(table_.size() * 2);

    // 2) move elements from the old table to the new one
    for (auto& bucket : table_) {
      for (auto& elem : bucket) {
        size_t i = hasher_(elem) % new_table.size();
        new_table[i].push_back(std::move(elem));
      }
    }

    // 3) replace old table with new one
    table_ = std::move(new_table);
  }
};

#endif  // HASH_SET_COARSE_GRAINED_H
