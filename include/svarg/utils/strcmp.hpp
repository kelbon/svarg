#pragma once

#include <string_view>
#include <algorithm>
#include <cctype>

namespace svarg {

// case insensitive
inline bool is_equal_ci(std::string_view a, std::string_view b) {
  return std::ranges::equal(
      a, b, [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
}

struct str_equal_to {
  using is_transparent = int;

  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return a == b;
  }
};

struct str_hash {
  using is_transparent = int;

  size_t operator()(std::string_view str) const noexcept {
    return std::hash<std::string_view>{}(str);
  }
};

}  // namespace svarg
