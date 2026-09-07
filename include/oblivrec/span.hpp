// ==========================================================================
//  span.hpp -- a minimal non-owning view.
//
//  WHY THIS EXISTS. design/ARCHITECTURE-draft-v1.md section 7.1 writes the
//  EvalFull signature with std::span, but std::span is C++20 and
//  REQUIREMENTS.md section 9 mandates C++17. Rather than bump the standard
//  for one type, we keep the stated C++17 contract and supply the 3% of
//  std::span that this project actually uses.
//
//  Recorded as a Decisions Log entry in design/ARCHITECTURE-draft-v1.md.
// ==========================================================================
#ifndef OBLIVREC_SPAN_HPP
#define OBLIVREC_SPAN_HPP

#include <cstddef>
#include <vector>
#include <type_traits>

// --------------------------------------------------------------------------
//  Bounds checking.
//
//  Span is the ONLY unchecked raw-pointer indexing in this codebase, and it
//  carries the deserialisation path, which parses attacker-controlled bytes.
//  std::vector is covered by _GLIBCXX_DEBUG and _GLIBCXX_ASSERTIONS, but those
//  do nothing for a hand-written type, so the hardened build was silently
//  leaving this one path unverified.
//
//  Checking is on whenever the libstdc++ hardening macros are on, so
//  `mingw32-make check` and the sanitizer builds get it, and release builds
//  pay nothing.
// --------------------------------------------------------------------------
#if defined(_GLIBCXX_ASSERTIONS) || defined(_GLIBCXX_DEBUG) || \
    defined(OBLIVREC_CHECKED_SPAN)
#  include <cstdio>
#  include <cstdlib>
#  define OBLIVREC_SPAN_CHECK(i, n)                                          \
     do {                                                                    \
       if (static_cast<std::size_t>(i) >= static_cast<std::size_t>(n)) {      \
         std::fprintf(stderr,                                                \
                      "FATAL: Span index %zu out of bounds (size %zu) at "   \
                      "%s:%d\n",                                             \
                      static_cast<std::size_t>(i),                           \
                      static_cast<std::size_t>(n), __FILE__, __LINE__);      \
         std::abort();                                                       \
       }                                                                     \
     } while (0)
#else
#  define OBLIVREC_SPAN_CHECK(i, n) ((void)0)
#endif

namespace oblivrec {

template <typename T>
class Span {
 public:
  using element_type = T;
  using value_type   = typename std::remove_cv<T>::type;

  constexpr Span() noexcept : data_(nullptr), size_(0) {}
  constexpr Span(T* p, std::size_t n) noexcept : data_(p), size_(n) {}

  template <std::size_t N>
  constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

  // Non-const vector -> Span<T> or Span<const T>.
  Span(std::vector<value_type>& v) noexcept
      : data_(v.data()), size_(v.size()) {}

  // Const vector -> Span<const T> only. SFINAE keeps this out of overload
  // resolution when T is non-const, so a const vector cannot silently
  // produce a mutable view.
  template <typename U = T,
            typename = typename std::enable_if<std::is_const<U>::value>::type>
  Span(const std::vector<value_type>& v) noexcept
      : data_(v.data()), size_(v.size()) {}

  constexpr T* data() const noexcept { return data_; }
  constexpr std::size_t size() const noexcept { return size_; }
  constexpr bool empty() const noexcept { return size_ == 0; }

  T& operator[](std::size_t i) const {
    OBLIVREC_SPAN_CHECK(i, size_);
    return data_[i];
  }

  constexpr T* begin() const noexcept { return data_; }
  constexpr T* end()   const noexcept { return data_ + size_; }

  Span<T> subspan(std::size_t off, std::size_t n) const {
    OBLIVREC_SPAN_CHECK(off + n, size_ + 1);   // off + n must be <= size_
    return Span<T>(data_ + off, n);
  }

 private:
  T* data_;
  std::size_t size_;
};

}  // namespace oblivrec
#endif  // OBLIVREC_SPAN_HPP
