#pragma once

#include <bitset>
#include "svarg/errors.hpp"

#include "svarg/to_json_value.hpp"

#include "svarg/backend/cxx/support/types.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/validate.hpp"

#include "svarg/backend/cxx/support/utility_for_generated.hpp"

namespace svarg {

using cxx_support::fields_count;
using cxx_support::get_special_field;

template <cxx_support::an_fundamental T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    boost::json::value j = to_json(get_special_field(v));
    cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
    return j;
  }
};

template <cxx_support::an_array T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    boost::json::value j = to_json(get_special_field(v));
    cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
    return j;
  }
};

// сериализация для not невозможна, её нет

template <cxx_support::a_string_enum T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    return boost::json::string(v.values[std::to_underlying(get_special_field(v))]);
  }
};

template <cxx_support::an_enum T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    boost::json::value j = to_json(get_special_field(v));
    cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
    return j;
  }
};

template <cxx_support::an_anyof T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    // для anyOf игнорируется дискриминатор при сериализации, пользователь сам должен указать правильный
    boost::json::value j = to_json(get_special_field(v));
    cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
    return j;
  }
};

template <cxx_support::a_oneof T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    boost::json::value j = to_json(get_special_field(v));
    if constexpr (cxx_support::has_discriminator<T>) {
      if (j.is_object()) {
        std::string_view discriminator = get_special_field(v).visit(
            metel::matcher{[](metel::nothing_t) -> std::string_view {
                             throw serialize_error("cannot serialize empty oneOf");
                           },
                           [](auto&& var) -> std::string_view { return T::discriminator_for(var); }});
        // на случай если пользователь не заполнил или заполнил неверно
        j[T::discriminator_field] = boost::json::string(discriminator);
      }
    }
    cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
    return j;
  }
};

template <cxx_support::an_allof T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    boost::json::value j = to_json(get_special_field(v));
    cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
    return j;
  }
};

template <cxx_support::an_object A>
boost::json::value aggregate_to_json(const A& v) {
  boost::json::value j;
  boost::json::object& obj = j.emplace_object();

  auto serialize_one = [&]<size_t I>(std::index_sequence<I>) {
    const auto& field = cxx_support::get_field<I>(v);
    constexpr std::string_view field_name = cxx_support::get_field_name<I, A>();
    if (cxx_support::is_field_present<I>(v))
      obj[field_name] = to_json(field);
  };
  APPLY_INDEXES(fields_count<A>()) {
    (serialize_one(std::index_sequence<Is>{}), ...);
  };
  if constexpr (cxx_support::has_additional_properties<A>) {
    for (auto&& [name, value] : cxx_support::get_additional_properties(v))
      obj[name] = to_json(value);
  }
  cxx_support::validate_required_schemas(j, v).rethrow_if_failed();
  return obj;
}

template <cxx_support::an_object T>
struct to_json_value<T> {
  static boost::json::value operator()(const T& v) {
    static_assert(cxx_support::valid_generated_object<T>());
    return aggregate_to_json(v);
  }
};

template <cxx_support::a_body T>
struct to_json_value<T> {
  // сознательно игнорируем content_type, на случай если кто-то захотел сериализовать как json тело с каким-то
  // другим медиа типом
  static boost::json::value operator()(const T& v) {
    // encoding игнорируется, так как для application/json не имеет смысла
    return to_json(cxx_support::get_special_field(v));
  }
};

}  // namespace svarg
