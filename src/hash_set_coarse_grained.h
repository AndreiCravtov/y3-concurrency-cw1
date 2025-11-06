#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(const size_t capacity)
      : table_(capacity), table_size_(capacity), set_size_(0) {
    assert(capacity > 0);
  }

  bool Add(T elem) final {
    {
      // scope-lock for mutual exclusion
      scoped_mutex_t _(mutex_);

      auto& bucket = Bucket_(elem);

      // 3) return false on duplicate (loops over the elements in that bucket)
      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        return false;
      }

      // 4) insert and update size if not present
      bucket.push_back(std::move(elem));
      set_size_.fetch_add(1);
    }  // release lock

    // 5) apply resizing policy if needed
    if (Policy_()) Resize_();
    return true;
  }

  bool Remove(T elem) final {
    // scope-lock for mutual exclusion
    scoped_mutex_t _(mutex_);

    auto& bucket = Bucket_(elem);

    // find element position (returning early if doesn't exist)
    auto i = std::find(bucket.begin(), bucket.end(), elem);
    if (i == bucket.end()) return false;

    // remove element & decrement size
    bucket.erase(i);
    set_size_.fetch_sub(1);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    // scope-lock for mutual exclusion
    scoped_mutex_t _(mutex_);

    auto& bucket = Bucket_(elem);

    // return if found or not
    auto found = std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
    return found;
  }

  [[nodiscard]] size_t Size() const final { return set_size_.load(); }

 private:
  using table_t = std::vector<std::vector<T>>;
  using scoped_mutex_t = std::scoped_lock<std::mutex>;

  table_t table_;
  std::atomic<size_t> table_size_;  // cached version of `table_.size()`
  std::atomic<size_t> set_size_;  // tracks the number of elements in the table
  std::hash<T> hasher_;
  std::mutex mutex_;

  /**
   * Returns the bucket associated with the element.
   */
  std::vector<T>& Bucket_(T elem) {
    return table_[hasher_(elem) % table_size_.load()];
  }

  bool Policy_() const { return set_size_.load() / table_size_.load() > 4; }

  void Resize_() {
    const size_t old_capacity = table_size_.load();

    // scope-lock for mutual exclusion
    scoped_mutex_t _(mutex_);

    // if table size already increased, don't resize
    if (old_capacity != table_size_.load()) return;

    // 1) create a new empty table with double the number of buckets
    size_t new_capacity = old_capacity * 2;
    table_t new_table(new_capacity);

    // 2) move elements from the old table to the new one
    for (auto& bucket : table_) {
      for (auto& elem : bucket) {
        size_t i = hasher_(elem) % new_table.size();
        new_table[i].push_back(std::move(elem));
      }
    }

    // 3) replace old table with new one
    table_ = std::move(new_table);
    table_size_.store(new_capacity);
  }
};

#endif  // HASH_SET_COARSE_GRAINED_H
