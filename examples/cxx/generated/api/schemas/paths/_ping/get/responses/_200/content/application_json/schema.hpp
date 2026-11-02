#pragma once

#include "common_types.hpp"

namespace api::paths::_ping::get::responses::_200::content::application_json {

struct schema {
  static constexpr svarg::cxx_support::parse_tag_e svarg_parse_tag = svarg::cxx_support::parse_tag_e::object;

  std::string status;

  using svarg_additional_properties_type = void;

  static constexpr std::array<::svarg::cxx_support::field_info, 1> svarg_fields = {{
      {.name = "status", .required = true},
  }};

  static constexpr bool svarg_has_field = true;
  template <size_t I>
  [[nodiscard]] constexpr decltype(auto) svarg_field(this auto&& self) noexcept {
    if constexpr (I == 0)
      return ::svarg::noadl::forwardLike<decltype(self)>(self.status);
    else
      static_assert(false);
  }
};

}  // namespace api::paths::_ping::get::responses::_200::content::application_json
