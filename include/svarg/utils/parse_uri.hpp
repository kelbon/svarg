#pragma once

#include <charconv>
#include <string_view>
#include <stdexcept>
#include <cstdint>

namespace svarg {

struct parsed_uri {
  std::string_view scheme;
  std::string_view host;
  std::string_view port;
  std::string_view path;

  uint16_t get_port() const {
    if (port.empty()) {
      if (scheme == "http")
        return 80;
      else if (scheme == "https")
        return 443;
      else
        return 0;
    }
    uint16_t p;
    auto [ptr, ec] = std::from_chars(port.data(), port.data() + port.size(), p);
    if (ec != std::errc{} || ptr != port.data() + port.size())
      throw std::invalid_argument("bad port");
    return p;
  }

  std::string to_string() const {
    std::string result;

    if (!scheme.empty()) {
      result += scheme;
      result += "://";
    }

    result += host;

    if (!port.empty()) {
      result += ':';
      result += port;
    }

    if (!path.empty()) {
      if (path[0] != '/')
        result += '/';
      result += path;
    }

    return result;
  }
};

parsed_uri parse_uri(std::string_view uri);

}  // namespace svarg
