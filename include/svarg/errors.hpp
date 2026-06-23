#pragma once

#include <format>

namespace svarg {

struct svarg_exception : std::runtime_error {
  template <typename... Args>
  explicit svarg_exception(std::format_string<Args...> fmt, Args&&... args)
      : std::runtime_error(std::format(fmt, std::forward<Args>(args)...)) {
  }
  explicit svarg_exception(const char* p) : std::runtime_error(p) {
  }
};

struct parse_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct validate_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct semantic_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct render_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

}  // namespace svarg
