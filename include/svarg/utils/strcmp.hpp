#pragma once

#include <string_view>
#include <algorithm>
#include <cctype>
#include "svarg/utils/combined_hash.hpp"

namespace svarg {

inline char to_lower(char c) noexcept {
  // важно чтобы не было UB
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// case insensitive
inline std::strong_ordering compare_ci(std::string_view a, std::string_view b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    char ca = to_lower(a[i]);
    char cb = to_lower(b[i]);
    if (ca != cb)
      return ca <=> cb;
  }
  return a.size() <=> b.size();
}

inline bool is_equal_ci(std::string_view a, std::string_view b) {
  return compare_ci(a, b) == std::strong_ordering::equal;
}

struct str_less_ci {
  using is_transparent = int;

  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return compare_ci(a, b) == std::strong_ordering::less;
  }
};

struct str_less {
  using is_transparent = int;

  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return a < b;
  }
};

struct str_equal_to_ci {
  using is_transparent = int;

  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return compare_ci(a, b) == std::strong_ordering::equal;
  }
};

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

struct str_hash_ci {
  using is_transparent = int;

  size_t operator()(std::string_view str) const noexcept {
    combined_hash h;
    for (char c : str)
      h.add(to_lower(c));
    return h;
  }
};

}  // namespace svarg
