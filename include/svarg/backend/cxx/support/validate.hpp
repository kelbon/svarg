#pragma once

#include <boost/json/value.hpp>

#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/validate_base.hpp"

namespace svarg::cxx_support {

template <typename RequiredSchema, typename Schema>
error_code validate_schema(const boost::json::object& json, const Schema& result) {
  static_assert(!has_recursive_requirements<RequiredSchema>());
  if constexpr (noexport::requirement_needs_tokens_reuse<RequiredSchema>) {
    Schema schema;
    return try_from_json(json, schema);
  } else {
    return noexport::validate_parsed_object<RequiredSchema>(result);
  }
}

template <typename T>
error_code validate_required_schemas(const boost::json::value& json, const T& result) {
  static_assert(!has_recursive_requirements<T>());  //  избегаем бесконечной рекурсии

  error_code ec;

  auto validate_one = [&]<typename S>(metel::typelist<S>) {
    ec = validate_schema<S>(json, result);
    return !ec.failed();
  };
  auto do_validate = [&]<typename... Schemas>(metel::typelist<Schemas...>) {
    (validate_one(metel::typelist<Schemas>{}) && ...);
  };
  do_validate(required_schemas_t<T>{});

  return ec;
}

}  // namespace svarg::cxx_support
