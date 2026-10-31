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
  // значение должно подходить ровно под одну схему
  static error_code operator()(const boost::json::value& j, cxx_support::oneof<Types...>& v) {
    size_t success = 0;
    auto parse_one = [&]<typename T>(std::type_identity<T>) {
      T x;
      if (try_from_json(j, x).failed())
        return;
      if (++success == 1)
        v.template emplace<T>(std::move(x));
    };
    (parse_one(std::type_identity<Types>{}), ...);
    if (success == 0)
      return parse_error("oneOf failed to parse any schema in list");
    if (success > 1) {
      v = nullptr;
      return parse_error("oneOf parse failed, {} schemas parsed", success);
    }
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
