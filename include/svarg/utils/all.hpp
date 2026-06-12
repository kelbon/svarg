#pragma once

#include <array>

namespace svarg {

template <typename T, std::size_t N>
class ref_range {
  T* ptrs_[N];

 public:
  struct iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using reference = T&;
    using pointer = T*;
    using difference_type = std::ptrdiff_t;

    T** ptr_;

    constexpr reference operator*() const {
      return **ptr_;
    }
    constexpr pointer operator->() const {
      return *ptr_;
    }

    constexpr iterator& operator++() {
      ++ptr_;
      return *this;
    }
    constexpr iterator operator++(int) {
      auto tmp = *this;
      ++ptr_;
      return tmp;
    }

    constexpr bool operator==(const iterator& o) const {
      return ptr_ == o.ptr_;
    }
    constexpr bool operator!=(const iterator& o) const {
      return ptr_ != o.ptr_;
    }
  };

  template <typename... Args>
  explicit constexpr ref_range(Args&... args) : ptrs_{&args...} {
  }

  constexpr iterator begin() {
    return {ptrs_};
  }
  constexpr iterator end() {
    return {ptrs_ + N};
  }
};

template <typename First, typename... Rest>
constexpr auto all(First& first, Rest&... rest) {
  static_assert((std::is_same_v<First, Rest> && ...), "All arguments must be the same type");

  return ref_range<First, 1 + sizeof...(Rest)>(first, rest...);
}

}  // namespace svarg
