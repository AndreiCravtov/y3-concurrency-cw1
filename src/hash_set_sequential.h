#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <vector>
#include <functional>
#include <algorithm>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(const size_t capacity) : table_(capacity), set_size_(0) {
    assert(capacity > 0);
  }

  bool Add(T elem) final {
    // 1) compute bucket index
    const size_t i = hasher_(elem) % table_.size();

    // 2) refer to that bucket
    auto& bucket = table_[i];

    // 3) return false on duplicate (loops over the elements in that bucket)
    for (const auto& x : bucket) {
      if (x == elem) return false;
    }

    // 4) insert and update size if not present
    bucket.push_back(std::move(elem));
    set_size_++;

    // 5) apply resizing policy if needed
    if (Policy_()) {
      Resize_();
    }
    return true;
  }

  bool Remove(T elem) final {
    // compute bucket index & find bucket
    auto i = hasher_(elem) % table_.size();
    auto& bucket = table_[i];

    // find element position (returning early if doesn't exist)
    auto pos = std::find(bucket.begin(), bucket.end(), elem);
    if (pos == bucket.end()) return false;

    // remove element & decrement size
    bucket.erase(pos);
    set_size_--;
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    auto& bucket = Bucket_(elem);

    // return if found or not
    auto found = std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
    return found;
  }

  [[nodiscard]] size_t Size() const final {
    return set_size_;
  }

  private:
    std::vector<std::vector<T>> table_;
    size_t set_size_; //tracks the number of elements in the table
    std::hash<T> hasher_;

    bool Policy_() {
      return set_size_ / table_.size() > 4;
    }

    void Resize_() {
      // 1) create a new empty table with double the number of buckets
      std::vector<std::vector<T>> new_table(table_.size() * 2);

      // 2) move elements from the old table to the new one
      for (auto& bucket : table_) {
        for (auto& elem : bucket) {
          size_t i = std::hash<T>()(elem) % new_table.size();
          new_table[i].push_back(std::move(elem));
        }
      }

      // 3) replace old table with new one
      table_ = std::move(new_table);
    }

    /**
     * Returns the bucket associated with the element.
     */
    std::vector<T>& Bucket_(T elem) {
      return table_[hasher_(elem) % table_.size()];
    }
};

#endif  // HASH_SET_SEQUENTIAL_H
