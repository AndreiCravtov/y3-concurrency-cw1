#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <cassert>
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
      locks_(capacity),
      owner_(nullptr) {
    assert(capacity > 0);
  }

  bool Add(T elem) final {
    std::vector<T> bucket = Bucket_(elem);
    std::unique_lock<std::mutex> lock = Lock_(elem);


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
  std::atomic<size_t> table_size_;
  std::atomic<size_t> size_;
  std::vector<std::unique_lock<std::mutex>> locks_;
  std::hash<T> hasher_;

  /**
   * Contains Ptr to std::thread::id of the thread that owns lock
   * False - no one uses, True - someone is using Hashset
   */
  AtomicMarkablePtr<std::thread::id> owner_;

  /**
   * Returns the bucket associated with the element
   */
  std::vector<T>& Bucket_(T elem) {
    return table_[hasher_(elem) % table_size_.load()];
  }

  /**
   * Returns the lock associated with the element
   */
  std::unique_lock<std::mutex> Lock_(T elem) {
    return locks_[hasher_(elem) % table_size_.load()];
  }

  /**
   * Acquires corresponding to the item lock
   * 1) Spins until no other thread is resizing the set
   * 2) Reads the lock array
   * 3) Acquires lock and checks again (1)
   */
  void Acquire_(T elem) {
    std::thread::id me = std::this_thread::get_id();
    std::thread::id* who;
    bool mark = true;
    while (true) {
      do {
        who = owner_.Get(mark);
      } while (mark && *who != me);

      std::unique_lock<std::mutex> old_lock = Lock_(elem);
      old_lock.lock();

      who = owner_.Get(mark);

      if ((!mark || *who != me) /* && old_locks == locks_ */) {
        return;
      }
      old_lock.unlock();
    }
  }

  /**
   * Releases corresponding to the item lock
   */
  void Release_(T elem) {
    Lock_(elem).unlock();
  }

  /**
   * Waits for all locks to be unlocked
   */
  void Quiesce_() {
    for (auto lock : locks_) {
      lock.lock();
      lock.unlock();
    }
  }

  /**
   * Resizes the table
   */

  void Resize_() {
    // TODO
  }
};

#endif  // HASH_SET_REFINABLE_H
