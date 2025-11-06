#ifndef UTIL_H
#define UTIL_H

#include <atomic>
#include <cassert>
#include <cstddef>
#include <thread>
#include <type_traits>

template <typename T1, typename T2>
struct pair {
  T1 _1;
  T2 _2;
};

class mutex_vector {
 public:
  explicit mutex_vector(const size_t capacity) : mutexes_(capacity) {}

  void lock() {
    for (auto& m : mutexes_) {
      m.lock();
    }
  }

  void unlock() {
    // unlock in reverse order
    for (auto& m : std::ranges::reverse_view(mutexes_)) {
      m.unlock();
    }
  }

  std::vector<std::mutex>& as_ref() & { return mutexes_; }

 private:
  std::vector<std::mutex> mutexes_;
};

template <typename T>
class AtomicMarkableValue {
 public:
  AtomicMarkableValue(T initial_value, const bool initial_mark)
      : marked_value_(MarkedValue_(initial_value, initial_mark)) {}

  T GetValue() { return marked_value_.load(std::memory_order::seq_cst)._1; }

  bool IsMarked() const {
    return marked_value_.load(std::memory_order::seq_cst)._2;
  }

  T Get(bool& mark_holder) {
    const auto marked_value = marked_value_.load(std::memory_order::seq_cst);

    mark_holder = marked_value._2;
    return marked_value._1;
  }

  bool CompareAndSet(T expected_value, T new_value, const bool expected_mark,
                     const bool new_mark) {
    auto expected_marked_value = MarkedValue_(expected_value, expected_mark);
    return marked_value_.compare_exchange_strong(
        expected_marked_value, MarkedValue_(new_value, new_mark),
        std::memory_order::seq_cst);
  }

  void Set(T new_value, const bool new_mark) {
    marked_value_.store(MarkedValue_(new_value, new_mark),
                        std::memory_order::seq_cst);
  }

 private:
  std::atomic<pair<T, bool>> marked_value_;

  [[nodiscard]] static pair<T, bool> MarkedValue_(T value, const bool mark) {
    return pair{._1 = value, ._2 = mark};
  }
};

/**
 * An `AtomicMarkablePtr` maintains an object pointer along with a mark
 * bit, that can be updated atomically.
 *
 * @tparam T the type of object referred to by this pointer
 */
template <typename T>
  requires(std::alignment_of_v<T> > 1)
class AtomicMarkablePtr {
 public:
  /**
   * Creates a new `AtomicMarkablePtr` with the given initial values.
   *
   * @param initial_ptr the initial pointer
   * @param initial_mark the initial mark
   */
  explicit AtomicMarkablePtr(T* initial_ptr = nullptr,
                             const bool initial_mark = false)
      : marked_ptr_(MarkedPtr_(initial_ptr, initial_mark)) {}

  /**
   * Returns the current value of the pointer.
   *
   * @return the current value of the pointer
   */
  [[nodiscard]] T* GetPtr() { return ExtractPtr_(marked_ptr_.load()); }

  /**
   * Returns the current value of the mark.
   *
   * @return the current value of the mark
   */
  [[nodiscard]] bool IsMarked() const {
    return ExtractMark_(marked_ptr_.load());
  }

  /**
   * Returns the current values of both the pointer and the mark.
   * Typical usage is `bool holder; auto ptr = v.Get(holder);` .
   *
   * @param mark_holder a pointer to a bool value. On return, mark_holder will
   *                    hold the value of the mark.
   * @return the current value of the pointer
   */
  [[nodiscard]] T* Get(bool& mark_holder) {
    const auto marked_ptr = marked_ptr_.load();

    mark_holder = ExtractMark_(marked_ptr);
    return ExtractPtr_(marked_ptr);
  }

  /**
   * Atomically sets the value of both the pointer and mark to the given
   * update values if the current pointer is == (i.e. equal) to the expected
   * pointer and the current mark is equal to the expected mark.
   *
   * @param expected_ptr the expected value of the pointer
   * @param new_ptr the new value for the pointer
   * @param expected_mark the expected value of the mark
   * @param new_mark the new value for the mark
   * @return true if successful
   */
  [[nodiscard]] bool CompareAndSet(T* expected_ptr, T* new_ptr,
                                   const bool expected_mark,
                                   const bool new_mark) {
    auto expected_marked_ptr = MarkedPtr_(expected_ptr, expected_mark);
    return marked_ptr_.compare_exchange_strong(expected_marked_ptr,
                                               MarkedPtr_(new_ptr, new_mark));
  }

  /**
   * Unconditionally sets the value of both the pointer and mark.
   *
   * @param new_ptr the new value for the pointer
   * @param new_mark the new value for the mark
   */
  void Set(T* new_ptr, const bool new_mark) {
    marked_ptr_.store(MarkedPtr_(new_ptr, new_mark));
  }

  // /**
  //  * Atomically sets the value of the mark to the given update value if the
  //  * current pointer is == (i.e. equal) to the expected pointer.
  //  *
  //  * @param expected_ptr the expected value of the pointer
  //  * @param new_mark the new value for the mark
  //  * @return true if successful
  //  */

  // bool AttemptMark(T* expected_ptr, bool new_mark) {
  //   static_assert(false, "not implemented");
  //   return false;
  // }

 private:
  std::atomic<uintptr_t> marked_ptr_;
  static constexpr uintptr_t mask_ = 1;

  [[nodiscard]] static uintptr_t MarkedPtr_(T* ptr, const bool mark) {
    // last bit should be 0 because `alignment > 1`
    const auto u_ptr = reinterpret_cast<uintptr_t>(ptr);
    assert((u_ptr & ~mask_) == u_ptr);

    // use LSB to encode mark into pointer
    return u_ptr | (mark ? 1 : 0);
  }

  [[nodiscard]] static T* ExtractPtr_(const uintptr_t marked_ptr) {
    return reinterpret_cast<T*>(marked_ptr & ~mask_);
  }

  [[nodiscard]] static bool ExtractMark_(const uintptr_t marked_ptr) {
    return marked_ptr & mask_;
  }
};

#endif  // UTIL_H
