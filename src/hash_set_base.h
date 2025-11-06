#ifndef HASH_SET_BASE_H
#define HASH_SET_BASE_H

#include <atomic>
#include <cassert>
#include <cstddef>
#include <type_traits>

template <typename T>
class HashSetBase {
 public:
  virtual ~HashSetBase() = default;

  // Adds |elem| to the hash set. Returns true if |elem| was absent, and false
  // otherwise.
  virtual bool Add(T elem) = 0;

  // Removes |elem| from the hash set. Returns true if |elem| was present, and
  // false otherwise.
  virtual bool Remove(T elem) = 0;

  // Returns true if |elem| is present in the hash set, and false otherwise.
  [[nodiscard]] virtual bool Contains(T elem) = 0;

  // Returns the size of the hash set.
  [[nodiscard]] virtual size_t Size() const = 0;
};

template <typename V>
  requires(std::alignment_of_v<V> > 1)
class AtomicMarkableReference {
 public:
  /**
   * Creates a new `AtomicMarkableReference` with the given initial values.
   *
   * @param initial_ref the initial reference
   * @param initial_mark the initial mark
   */
  explicit AtomicMarkableReference(V& initial_ref, const bool initial_mark)
      : marked_ref_(MarkRef_(initial_ref, initial_mark)) {}

  /**
   * Returns the current value of the reference.
   *
   * @return the current value of the reference
   */
  [[nodiscard]] V& GetReference() { return ExtractRef_(marked_ref_.load()); }

  /**
   * Returns the current value of the mark.
   *
   * @return the current value of the mark
   */
  [[nodiscard]] bool IsMarked() const {
    return ExtractMark_(marked_ref_.load());
  }

  /**
   * Returns the current values of both the reference and the mark.
   * Typical usage is `bool holder; auto ref = v.Get(holder);` .
   *
   * @param mark_holder a reference to a bool value. On return, mark_holder will
   *                    hold the value of the mark.
   * @return the current value of the reference
   */
  [[nodiscard]] V& Get(bool& mark_holder) {
    const auto marked_ref = marked_ref_.load();

    mark_holder = ExtractMark_(marked_ref);
    return ExtractRef_(marked_ref);
  }

  /**
   * Atomically sets the value of both the reference and mark to the given
   * update values if the current reference is == (i.e. equal) to the expected
   * reference and the current mark is equal to the expected mark.
   *
   * @param expected_ref the expected value of the reference
   * @param new_ref the new value for the reference
   * @param expected_mark the expected value of the mark
   * @param new_mark the new value for the mark
   * @return true if successful
   */
  [[nodiscard]] bool CompareAndSet(V& expected_ref, V& new_ref,
                                   const bool expected_mark,
                                   const bool new_mark) {
    return marked_ref_.compare_exchange_strong(
        MarkRef_(expected_ref, expected_mark), MarkRef_(new_ref, new_mark));
  }

  /**
   * Unconditionally sets the value of both the reference and mark.
   *
   * @param new_ref the new value for the reference
   * @param new_mark the new value for the mark
   */
  void Set(V& new_ref, const bool new_mark) {
    marked_ref_.store(MarkRef_(new_ref, new_mark));
  }

  // /**
  //  * Atomically sets the value of the mark to the given update value if the
  //  * current reference is == (i.e. equal) to the expected reference.
  //  *
  //  * @param expected_ref the expected value of the reference
  //  * @param new_mark the new value for the mark
  //  * @return true if successful
  //  */

  // bool AttemptMark(V& expected_ref, bool new_mark) {
  //   static_assert(false, "not implemented");
  //   return false;
  // }

 private:
  std::atomic<uintptr_t> marked_ref_;
  static constexpr uintptr_t mask_ = 1;

  [[nodiscard]] static uintptr_t MarkRef_(V& ref, const bool mark) {
    // last bit should be 0 because `alignment > 1`
    const auto ptr = static_cast<uintptr_t>(&ref);
    assert((ptr & ~mask_) == ptr);

    // use last bit to encode mark into reference
    return ptr | (mark ? 1 : 0);
  }

  [[nodiscard]] static V& ExtractRef_(const uintptr_t marked_ref) {
    V* ptr = static_cast<V*>(marked_ref & ~mask_);
    return *ptr;
  }

  [[nodiscard]] static bool ExtractMark_(const uintptr_t marked_ref) {
    return marked_ref & mask_;
  }
};

#endif  // HASH_SET_BASE_H
