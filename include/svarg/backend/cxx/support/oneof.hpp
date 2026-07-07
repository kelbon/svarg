#pragma once

#include <metel/box_union.hpp>

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"

namespace svarg::cxx_support {

template <typename... Types>
using oneof = metel::box_union<Types...>;

}

namespace svarg {

template <typename... Types>
struct from_json_value<cxx_support::oneof<Types...>> {
  static error_code operator()(const boost::json::value& j, cxx_support::oneof<Types...>& v) {
    ;  // TODO;
  }
};

template <typename... Types>
struct to_json_value<cxx_support::oneof<Types...>> {
  static boost::json::value operator()(const cxx_support::oneof<Types...>& v) {
    ;  // TODO;
  }
};

}  // namespace svarg
