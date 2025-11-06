#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <thread>
#include <variant>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(const size_t capacity)
      : table_(capacity),
        table_size_(capacity),
        set_size_(0),
        mutexes_(capacity),
        resizing_(false) {
    assert(capacity > 0);
  }

  bool Add(T elem) final {
    {
      Acquire_(elem);
      auto& bucket = Bucket_(elem);

      // 3) return false on duplicate (loops over the elements in that bucket)
      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        Release_(elem);
        return false;
      }

      // 4) insert and update size if not present
      bucket.push_back(std::move(elem));
      set_size_.fetch_add(1);

      Release_(elem);
    }

    // 5) apply resizing policy if needed
    if (Policy_()) Resize_();
    return true;
  }

  bool Remove(T elem) final {
    // scope-lock for mutual exclusion
    Acquire_(elem);
    auto& bucket = Bucket_(elem);

    // find element position (returning early if doesn't exist)
    auto i = std::find(bucket.begin(), bucket.end(), elem);
    if (i == bucket.end()) {
      Release_(elem);
      return false;
    }

    // remove element & decrement size
    bucket.erase(i);
    set_size_.fetch_sub(1);
    Release_(elem);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    // scope-lock for mutual exclusion
    Acquire_(elem);
    auto& bucket = Bucket_(elem);

    // return if found or not
    auto found = std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
    Release_(elem);
    return found;
  }

  [[nodiscard]] size_t Size() const final { return set_size_.load(); }

 private:
  using table_t = std::vector<std::vector<T>>;

  table_t table_;
  std::atomic<size_t> table_size_;  // cached version of `table_.size()`
  std::atomic<size_t> set_size_;  // tracks the number of elements in the table
  std::hash<T> hasher_;
  std::vector<std::mutex> mutexes_;
  std::shared_mutex resize_mutex_;
  std::atomic<bool> resizing_;  // are we resizing or not

  /**
   * Returns the bucket associated with the element.
   */
  std::vector<T>& Bucket_(T elem) {
    return table_[hasher_(elem) % table_size_.load()];
  }

  /**
   * Returns the lock associated with the element.
   */
  auto& Mutex_(T elem) { return mutexes_[hasher_(elem) % mutexes_.size()]; }

  void Acquire_(T elem) {
    // don't try to acquire while resizing (heuristic)
    do {
    } while (resizing_.load());

    // acquire shared resize lock first, then the specific lock
    resize_mutex_.lock_shared();
    Mutex_(elem).lock();
  }

  void Release_(T elem) {
    // release in reverse order
    Mutex_(elem).unlock();
    resize_mutex_.unlock_shared();
  }

  [[nodiscard]] bool Policy_() const {
    return set_size_.load() / table_size_.load() > 4;
  }

  void Resize_() {
    const size_t old_capacity = table_size_.load();
    size_t new_capacity = old_capacity * 2;

    if (auto _false = false; resizing_.compare_exchange_strong(_false, true)) {
      // acquire exclusive resize lock -> no other thread can do anything now
      resize_mutex_.lock();

      // someone else resized first -> no longer resizing
      if (old_capacity != table_size_.load()) {
        resizing_.store(false);
        resize_mutex_.unlock();
        return;
      }

      // wait for all locks to be released & replace old locks with new ones
      Quiesce_();
      mutexes_ = std::vector<std::mutex>(new_capacity);

      // create a new empty table with double the bucket count
      // and rehash all elements into it from old table
      table_t new_table(new_capacity);
      for (auto& bucket : table_) {
        for (auto& elem : bucket) {
          size_t i = hasher_(elem) % new_table.size();
          new_table[i].push_back(std::move(elem));
        }
      }

      // replace old table with new one
      table_ = std::move(new_table);
      table_size_.store(new_capacity);

      resizing_.store(false);  // no longer resizing
      resize_mutex_.unlock();
    }
  }

  void Quiesce_() {
    for (auto& m : mutexes_) {
      {  // equivalent of `while (lock.isLocked()) {}` in Java
        while (!m.try_lock()) {
        }
        m.unlock();
      }
    }
  }
};

#endif  // HASH_SET_REFINABLE_H
