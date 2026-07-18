#pragma once

#include <string_view>
#include <algorithm>
#include <cctype>
#include "svarg/utils/combined_hash.hpp"

namespace svarg {

/*
обрабатывает также локаль, что вредно, так как HTTP не ожидает например понижения какой-то юникодной буквы и
сравнения content type сломаются

inline char to_lower(char c) noexcept {
  // важно чтобы не было UB
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline char to_upper(char c) noexcept {
  // важно чтобы не было UB
  return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}
*/

[[nodiscard]] constexpr char to_lower_ascii(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

[[nodiscard]] constexpr char to_upper_ascii(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c;
}

[[nodiscard]] constexpr bool is_lowered(std::string_view str) noexcept {
  for (char c : str) {
    if (c != to_lower_ascii(c))
      return false;
  }
  return true;
}
[[nodiscard]] constexpr std::string lowered(std::string_view input) {
  std::string out(input);
  for (char& c : out)
    c = to_lower_ascii(c);
  return out;
}

[[nodiscard]] constexpr std::string uppered(std::string_view input) {
  std::string out(input);
  for (char& c : out)
    c = to_upper_ascii(c);
  return out;
}

// case insensitive
constexpr std::strong_ordering compare_ci(std::string_view a, std::string_view b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    char ca = to_lower_ascii(a[i]);
    char cb = to_lower_ascii(b[i]);
    if (ca != cb)
      return ca <=> cb;
  }
  return a.size() <=> b.size();
}

constexpr bool starts_with_ci(std::string_view str, std::string_view prefix) noexcept {
  if (prefix.size() > str.size())
    return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = str[i];
    char b = prefix[i];
    if (to_lower_ascii(b) != to_lower_ascii(a))
      return false;
  }
  return true;
}

// precondition: prefix уже lowered
constexpr bool starts_with_ci_prefix_lowered(std::string_view str, std::string_view prefix) noexcept {
  if (prefix.size() > str.size())
    return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = str[i];
    char b = prefix[i];
    assert(b == to_lower_ascii(b));
    if (b != to_lower_ascii(a))
      return false;
  }
  return true;
}

constexpr bool is_equal_ci(std::string_view a, std::string_view b) {
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
      h.add(to_lower_ascii(c));
    return h;
  }
};

}  // namespace svarg
