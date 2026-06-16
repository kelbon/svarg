#pragma once

#include <string_view>
#include <algorithm>
#include <cctype>

namespace svarg {

// case insensitive
inline std::strong_ordering compare_ci(std::string_view a, std::string_view b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    unsigned char ca = std::tolower(static_cast<unsigned char>(a[i]));
    unsigned char cb = std::tolower(static_cast<unsigned char>(b[i]));
    if (ca != cb)
      return ca <=> cb;
  }
  return a.size() <=> b.size();
}

inline bool is_equal_ci(std::string_view a, std::string_view b) {
  return compare_ci(a, b) == std::strong_ordering::equal;
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
