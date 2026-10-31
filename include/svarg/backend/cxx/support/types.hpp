#pragma once

#include <compare>
#include <vector>

#include "svarg/backend/cxx/support/allof.hpp"
#include "svarg/backend/cxx/support/anyof.hpp"
#include "svarg/backend/cxx/support/oneof.hpp"
#include "svarg/backend/cxx/support/not.hpp"
#include "svarg/common_types.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/from_json_value.hpp"

namespace svarg::cxx_support {

struct null {
  std::strong_ordering operator<=>(const null&) const = default;
};

using integer = int64_t;
using number = ::svarg::number;
using ::svarg::arrayof;
using boolean = bool;
// схема без ограничений типа (type: true, {} и т.п.) - любое JSON значение
using any = boost::json::value;
// схема, которой не удовлетворяет ни одно значение (type: false, not: {}): разбор всегда ошибка
struct never {
  bool operator==(const never&) const = default;
};

template <typename T>
using additional_properties_t = string_map<T>;

}  // namespace svarg::cxx_support

namespace svarg {

template <>
struct from_json_value<cxx_support::never> {
  static error_code operator()(const boost::json::value& j, cxx_support::never&) {
    return validate_error("no value matches `never` schema, got `{}`", e2str(j.kind()));
  }
};

template <>
struct to_json_value<cxx_support::never> {
  [[noreturn]] static boost::json::value operator()(const cxx_support::never&) {
    throw serialize_error("value of `never` schema cannot be serialized");
  }
};

template <>
struct from_json_value<cxx_support::null> {
  static error_code operator()(const boost::json::value& j, const cxx_support::null&) {
    if (!j.is_null())
      return parse_error("expected `null`, got `{}`", e2str(j.kind()));
    return {};
  }
};

template <>
struct to_json_value<cxx_support::null> {
  static boost::json::value operator()(const cxx_support::null&) {
    return nullptr;
  }
};

}  // namespace svarg
