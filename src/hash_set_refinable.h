#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <ranges>
#include <thread>
#include <variant>
#include <vector>

#include "src/hash_set_base.h"
#include "src/util.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(const size_t capacity)
      : table_(capacity),
        table_size_(capacity),
        set_size_(0),
        mutexes_(capacity),
        owner_(std::nullopt, false) {
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
  using owner_t = std::optional<std::thread::id>;

  table_t table_;
  std::atomic<size_t> table_size_;  // cached version of `table_.size()`
  std::atomic<size_t> set_size_;  // tracks the number of elements in the table
  std::hash<T> hasher_;
  std::vector<std::mutex> mutexes_;
  AtomicMarkableValue<owner_t> owner_;  // which thread is resizing

  /**
   * Returns the bucket associated with the element.
   */
  std::vector<T>& Bucket_(T elem) {
    return table_[hasher_(elem) % table_size_.load()];
  }

  void Acquire_(T elem) {
    bool mark = true;
    const owner_t me = std::this_thread::get_id();
    owner_t who;

    while (true) {
      do {
        who = owner_.Get(mark);
      } while (mark && who != me);

      auto* old_locks = &mutexes_;
      auto& old_lock = old_locks->at(hasher_(elem) % old_locks->size());
      old_lock.lock();

      who = owner_.Get(mark);
      if ((!mark || who == me) && &mutexes_ == old_locks) {
        return;
      } else {
        old_lock.unlock();
      }
    }
  }

  void Release_(T elem) { mutexes_[hasher_(elem) % mutexes_.size()].unlock(); }

  bool Policy_() const { return set_size_.load() / table_size_.load() > 4; }

  void Resize_() {
    const size_t old_capacity = table_size_.load();
    // bool mark = false; // HUH??
    size_t new_capacity = old_capacity * 2;

    const owner_t me = std::this_thread::get_id();
    if (owner_.CompareAndSet(std::nullopt, me, false, true)) {
      // someone else resized first -> no longer resizing
      if (old_capacity != table_size_.load()) {
        owner_.Set(std::nullopt, false);
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

      owner_.Set(std::nullopt, false);  // no longer resizing
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
