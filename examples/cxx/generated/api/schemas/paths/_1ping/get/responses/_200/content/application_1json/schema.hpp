
#pragma once

#include "common_types.hpp"

namespace api::paths::_ping::get::responses::_200::content::application_json {

struct _schema {
  static constexpr svarg::cxx_support::parse_tag_e parse_tag = svarg::cxx_support::parse_tag_e::object;

  std::string status;

  using additional_properties_type = void;

  [[nodiscard]] static constexpr bool is_required(std::string_view str) noexcept {
    if (str == "status")
      return true;
    return false;
  }

  static constexpr std::array<std::string_view, 1> field_names = {
      "status",
  };
};

}  // namespace api::paths::_ping::get::responses::_200::content::application_json
