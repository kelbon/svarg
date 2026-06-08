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

}  // namespace svarg
