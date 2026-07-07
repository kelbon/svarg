#pragma once

#include <cstdint>

#include <string_view>

#include "svarg/from_json_value.hpp"

namespace svarg {

enum struct parameter_location : uint8_t {
  path,
  query,
  header,
  cookie,
  // TODO querystring (openapi 3.2)?
};

std::string_view e2str(parameter_location l) noexcept;

// возвращает false при ошибке
[[nodiscard]] bool efromstr(std::string_view s, parameter_location& out);

template <>
struct from_json_value<parameter_location> {
  error_code operator()(const boost::json::value& v, parameter_location& out) {
    if (const auto* s = v.if_string()) {
      if (efromstr(std::string_view(*s), out))
        return {};
      return bad_enum_value<parameter_location>(std::string_view(*s));
    }
    return parse_error(" string expected for parameter_location, got `{}`", e2str(v.kind()));
  }
};

}  // namespace svarg
