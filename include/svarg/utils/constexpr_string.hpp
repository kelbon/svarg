#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace svarg {

// не обязательно null terminated
template <size_t Size>
struct constexpr_string {
  std::array<char, Size> chars = {};

  constexpr constexpr_string() noexcept = default;

  constexpr constexpr_string(const char (&str)[Size + 1]) noexcept {
    std::copy_n(str, Size, chars.begin());
  }

  constexpr constexpr_string(std::string_view sv) {
    if (sv.size() != Size)
      throw std::runtime_error("invalid size");
    std::copy_n(sv.data(), sv.size(), chars.begin());
  }

  constexpr std::string_view str() const {
    return std::string_view{data(), size()};
  }

  constexpr const char* data() const {
    return chars.data();
  }

  static constexpr size_t size() noexcept {
    return Size;
  }

  static constexpr bool empty() noexcept {
    return size() == 0;
  }

  friend constexpr bool operator==(const constexpr_string& string, std::string_view rhs) {
    return string.str() == rhs;
  }
  friend constexpr bool operator==(std::string_view lhs, const constexpr_string& string) {
    return string == lhs;
  }
};

// пропуск нултерминатора
template <size_t Sz>
constexpr_string(const char (&)[Sz]) -> constexpr_string<Sz - 1>;

template <constexpr_string S>
constexpr auto operator""_cs() noexcept {
  return S;
}

}  // namespace svarg
