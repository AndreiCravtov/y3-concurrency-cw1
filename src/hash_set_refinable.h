#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "src/hash_set_base.h"
#include "util.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(const size_t capacity)
      : table_(capacity),
        table_size_(capacity),
        size_(0),
        mutexes_(capacity),
        is_resizing_(false) {
    assert(capacity > 0);
    mutexes_.resize(capacity);
    for (auto& m : mutexes_) {
      m = std::make_shared<std::mutex>();
    }
  }

  bool Add(T elem) final {
    // Acquire Lock
    Acquire_(elem);

    std::vector<T>& bucket = Bucket_(elem);

    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      Release_(elem);
      return false;
    }

    bucket.push_back(elem);
    size_.fetch_add(1);

    Release_(elem);
    if (Policy_()) Resize_();
    return true;
  }

  bool Remove(T elem) final {
    Acquire_(elem);

    std::vector<T>& bucket = Bucket_(elem);

    // find element position (returning early if doesn't exist)
    auto i = std::find(bucket.begin(), bucket.end(), elem);
    if (i == bucket.end()) {
      Release_(elem);
      return false;
    }

    bucket.erase(i);
    size_.fetch_sub(1);

    Release_(elem);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    Acquire_(elem);

    std::vector<T>& bucket = Bucket_(elem);

    if (std::find(bucket.begin(), bucket.end(), elem) == bucket.end()) {
      Release_(elem);
      return false;
    }

    Release_(elem);
    return true;
  }

  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::atomic<size_t> table_size_;
  std::atomic<size_t> size_;
  std::vector<std::shared_ptr<std::mutex>> mutexes_;

  std::hash<T> hasher_;

  std::atomic<bool> is_resizing_;

  /**
   * Returns the bucket associated with the element
   */
  std::vector<T>& Bucket_(T elem) {
    return table_[hasher_(elem) % table_size_.load()];
  }

  /**
   * Returns the mutex associated with the element's bucket
   */
  std::shared_ptr<std::mutex> Mutex_(T elem) {
    size_t idx = hasher_(elem) % table_size_.load();
    return mutexes_[idx];  // returns shared_ptr copy
  }

  /**
   * Acquires corresponding to the item lock
   * 1) Spins until no other thread is resizing the set
   * 2) Reads the lock array
   * 3) Acquires lock and checks again (1)
   */
  void Acquire_(T elem) {
    while (true) {
      while (is_resizing_.load()) {
      }

      std::mutex& mutex = *Mutex_(elem);
      mutex.lock();

      if (!is_resizing_.load() /* && old_locks == locks_ */) {
        return;
      }
      mutex.unlock();
      // Retry if resizing started mid-lock
    }
  }

  /**
   * Releases corresponding to the item lock
   */
  void Release_(T elem) {
    auto mtx = Mutex_(elem);
    mtx->unlock();
  }

  /**
   * Waits for all locks to be unlocked
   */
  void Quiesce_() {
    for (auto& mtx_ptr : mutexes_) {
      std::scoped_lock<std::mutex> lock(*mtx_ptr);
    }
  }

  bool Policy_() {
    return size_ / table_size_.load() > 4;  // average size > 4
  }

  /**
   * Doubles the table size
   */
  void Resize_() {
    size_t old_table_size = table_size_.load();
    size_t new_table_size = old_table_size * 2;

    bool expected = false;
    bool desired = true;

    if (is_resizing_.compare_exchange_strong(expected, desired)) {
      if (table_size_.load() != old_table_size) {
        // someone already successfully resized, just return
        is_resizing_.store(false);
        return;
      }

      // Wait until all bucket locks are released
      Quiesce_();

      std::vector<std::vector<T>> old_table = table_;
      table_ = std::vector<std::vector<T>>(new_table_size);

      std::vector<std::shared_ptr<std::mutex>> new_mutexes(new_table_size);
      for (auto& m : new_mutexes) {
        m = std::make_shared<std::mutex>();
      }
      mutexes_ = std::move(new_mutexes);

      for (auto& bucket : old_table) {
        for (auto& elem : bucket) {
          size_t i = hasher_(elem) % new_table_size;
          table_[i].push_back(elem);
        }
      }

      table_size_.store(new_table_size);

      is_resizing_ = false;
    }
  }
};

#endif  // HASH_SET_REFINABLE_H
