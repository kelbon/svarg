#pragma once

#include <metel/box_union.hpp>

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"

namespace svarg::cxx_support {

// TODO ? отдельный тип
template <typename... Types>
using oneof = metel::box_union<Types...>;

}  // namespace svarg::cxx_support

namespace svarg {

template <typename... Types>
struct from_json_value<cxx_support::oneof<Types...>> {
  static error_code operator()(const boost::json::value& j, cxx_support::oneof<Types...>& v) {
    size_t success = 0;
    ((success += !try_from_json(j, v.template get<Types>()).failed()), ...);
    if (success == 0)
      return parse_error("oneOf failed to parse any schema in list");
    if (success > 1)
      return parse_error("oneOf parse failed, two or more schemas parsed");
    return {};
  }
};

template <typename... Types>
struct to_json_value<cxx_support::oneof<Types...>> {
  static boost::json::value operator()(const cxx_support::oneof<Types...>& v) {
    return v.visit(metel::matcher{[](metel::nothing_t) -> boost::json::value {
                                    throw serialize_error("empty oneOf cannot be serialized");
                                  },
                                  [](auto&& x) { return to_json(x); }});
  }
};

}  // namespace svarg
