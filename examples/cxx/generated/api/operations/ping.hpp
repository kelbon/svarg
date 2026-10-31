
#pragma once

#include "common_types.hpp"

#include "api/schemas/paths/_1ping/get/responses/_200/content/application_1json/schema.hpp"

namespace api {

struct ping {
  static constexpr svarg::cxx_support::parse_tag_e parse_tag = svarg::cxx_support::parse_tag_e::operation;

  static constexpr inline std::string_view spec = "api";

  static constexpr inline std::string_view method = "GET";
  static constexpr void visit_path(auto&& vtor) {
    vtor.substr("/ping");
  }

  static constexpr std::string_view path_expr = "/ping";

  struct response_200 {
    static constexpr svarg::cxx_support::parse_tag_e parse_tag = svarg::cxx_support::parse_tag_e::response;

    static constexpr int status = 200;

    static constexpr std::string_view body_content_type = "application/json";
    static constexpr bool body_is_required = true;

    paths::_ping::get::responses::_200::content::application_json::schema body;
  };

  static constexpr decltype(auto) visit_response_status(int status, auto&& vtor) {
    if (status == 200)
      return vtor.template operator()<response_200>();
    return vtor.template operator()<void>();
  }

  using result_t = response_200;
};

}  // namespace api
