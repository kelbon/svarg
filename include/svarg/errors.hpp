#pragma once

#include <format>

namespace svarg {

struct parse_error : std::runtime_error {
  template <typename... Args>
  explicit parse_error(std::format_string<Args...> fmt, Args&&... args)
      : std::runtime_error(std::format(fmt, std::forward<Args>(args)...)) {
  }
  explicit parse_error(const char* p) : std::runtime_error(p) {
  }
};

struct validate_error : parse_error {
  using parse_error::parse_error;
  using parse_error::operator=;
};

}  // namespace svarg
