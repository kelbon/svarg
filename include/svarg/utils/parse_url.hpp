#pragma once

#include <string_view>
#include <stdexcept>
#include <cstdint>

#include "svarg/utils/from_chars.hpp"
#include "svarg/utils/strcmp.hpp"

namespace svarg {

struct url_ref {
  std::string_view scheme;
  std::string_view userinfo;  // "user:pass" (без '@'), обычно пуст
  std::string_view host;      // для IPv6 - БЕЗ квадратных скобок
  std::string_view port;
  std::string_view path;  // может начинаться с '/', '?' или '#'
  bool is_ipv6 = false;

  // forbid url_ref(str)
  url_ref() = default;

  uint16_t get_port() const {
    if (port.empty()) {
      if (is_equal_ci(scheme, "http"))
        return 80;
      if (is_equal_ci(scheme, "https"))
        return 443;
      return 0;
    }
    uint16_t p;
    if (!try_from_chars(port, p))
      throw std::invalid_argument("bad port");
    return p;
  }

  std::string to_string() const {
    std::string result;

    if (!scheme.empty()) {
      result += scheme;
      result += "://";
    }

    if (!userinfo.empty()) {
      result += userinfo;
      result += '@';
    }

    if (is_ipv6) {
      result += '[';
      result += host;
      result += ']';
    } else {
      result += host;
    }

    if (!port.empty()) {
      result += ':';
      result += port;
    }

    if (!path.empty()) {
      if (path[0] != '/' && path[0] != '?' && path[0] != '#')
        result += '/';
      result += path;
    }

    return result;
  }
};

constexpr bool is_scheme_char(char c, bool first) {
  switch (c) {
    case 'A' ... 'Z':
    case 'a' ... 'z':
      return true;
    case '0' ... '9':
    case '+':
    case '-':
    case '.':
      return !first;
    default:
      return false;
  }
}

[[nodiscard]] constexpr bool is_valid_scheme(std::string_view str) noexcept {
  if (str.empty())
    return false;
  bool first = true;
  for (char c : str) {
    if (!is_scheme_char(c, /*first=*/first))
      return false;
    first = false;
  }
  return true;
}

[[nodiscard]] constexpr bool is_relative_url(std::string_view s) noexcept {
  auto pos = s.find("://");
  if (pos == std::string_view::npos)
    return true;
  return !is_valid_scheme(s);
}

[[nodiscard]] url_ref parse_absolute_url(std::string_view uri);

}  // namespace svarg
