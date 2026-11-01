#pragma once

#include <bitset>
#include "svarg/errors.hpp"

#include "svarg/from_json_value.hpp"

#include "svarg/backend/cxx/support/types.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/validate.hpp"

#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/utils/constexpr_string.hpp"

#include <boost/json/parse.hpp>

namespace svarg {

using cxx_support::fields_count;
using cxx_support::get_special_field;

template <cxx_support::an_fundamental T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    error_code ec = try_from_json(j, get_special_field(v));
    if (ec)
      return ec;
    return cxx_support::validate_required_schemas(j, v);
  }
};

template <cxx_support::an_array T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    error_code ec = try_from_json(j, get_special_field(v));
    if (ec)
      return ec;
    return cxx_support::validate_required_schemas(j, v);
  }
};

template <cxx_support::a_not_a T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    error_code ec = try_from_json(j, get_special_field(v));
    if (ec)
      return ec;
    return cxx_support::validate_required_schemas(j, v);
  }
};

template <cxx_support::a_string_enum T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    // по сути парсер для строки + встроенная валидация
    if (!j.is_string())
      return parse_error("expected string, got `{}`", e2str(j.kind()));
    std::string_view str = j.get_string();
    auto it = std::ranges::find(T::values, str);
    if (it == T::values.end())
      return validate_error("string {} cannot be value of enum {}", str, type_name<T>());
    // енам генерируется так, что порядок имён совпадает с порядком в массиве
    get_special_field(v) = typename T::values_e(std::distance(T::values.begin(), it));
    return {};
  }
};

template <cxx_support::an_enum T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    using underlying = std::decay_t<decltype(get_special_field(v))>;

    auto invalid_value = [](auto&& v) {
      return validate_error("value {} cannot represent enum {}", v, type_name<T>());
    };
    // рендер не создаёт енамов нетривиального типа
    // только bool, int64_t, double, null
    if constexpr (std::is_same_v<underlying, cxx_support::integer>) {
      cxx_support::integer i;
      if (j.is_int64())
        i = j.get_int64();
      else if (j.is_uint64())
        i = j.get_uint64();
      else
        return parse_error("expected uint64|int64_t, got {}", e2str(j.kind()));
      if (!std::ranges::contains(T::values, i))
        return invalid_value(i);
      get_special_field(v) = i;
    } else if constexpr (std::is_same_v<underlying, cxx_support::boolean>) {
      if (!j.is_bool())
        return parse_error("expected bool, got {}", e2str(j.kind()));
      cxx_support::boolean b = j.get_bool();
      if (!std::ranges::contains(T::values, b))
        return invalid_value(b);
      get_special_field(v) = b;
    } else if constexpr (std::is_same_v<underlying, number>) {
      number d;
      if (j.is_int64())
        d = j.get_int64();
      else if (j.is_uint64())
        d = j.get_uint64();
      else if (j.is_double())
        d = j.get_double();
      else  // throw
        return parse_error("expected uint64|int64_t|double, got {}", e2str(j.kind()));
      if (!std::ranges::contains(T::values, d))  // сравнивает на ==, плевать
        return invalid_value(d);
      get_special_field(v) = d;
    } else if constexpr (std::is_same_v<underlying, cxx_support::null>) {
      if (!j.is_null())
        return parse_error("expected null, got {}", e2str(j.kind()));
    } else {
      // C++23 позволяет так делать
      static_assert(false, "unsupported enum type");
    }
    return {};
  }
};

template <cxx_support::an_anyof T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    if constexpr (cxx_support::has_discriminator<T>) {
      if (!j.is_object()) {
        // игнорируем дискриминатор на случай если фундаментальный тип
        // TODO пытаться парсить только фундаментальные типы из anyOf
        error_code ec = try_from_json(j, get_special_field(v));
        if (ec)
          return ec;
        return cxx_support::validate_required_schemas(j, v);
      }
      auto& obj = j.as_object();
      auto* discriminator = obj.if_contains(T::discriminator_value);
      return T::discriminate(std::string_view(*discriminator), [&]<typename X>() {
        if constexpr (std::is_void_v<X>) {
          return parse_error("invalid discriminator value `{}` for anyOf `{}`",
                             std::string_view(*discriminator), type_name<X>());
        } else {
          return try_from_json(j, v);
        }
      });
    } else {
      error_code ec = try_from_json(j, get_special_field(v));
      if (ec)
        return ec;
      return cxx_support::validate_required_schemas(j, v);
    }
  }
};

template <cxx_support::a_oneof T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    if constexpr (cxx_support::has_discriminator<T>) {
      if (!j.is_object()) {
        // игнорируем дискриминатор на случай если фундаментальный тип
        // TODO пытаться парсить только фундаментальные типы из oneOf
        error_code ec = try_from_json(j, get_special_field(v));
        if (ec)
          return ec;
        return cxx_support::validate_required_schemas(j, v);
      }
      auto& obj = j.as_object();
      auto* discriminator = obj.if_contains(T::discriminator_value);
      return T::discriminate(std::string_view(*discriminator), [&]<typename X>() {
        if constexpr (std::is_void_v<X>) {
          return parse_error("invalid discriminator value `{}` for oneOf `{}`",
                             std::string_view(*discriminator), type_name<X>());
        } else {
          return try_from_json(j, v);
        }
      });
    } else {
      error_code ec = try_from_json(j, get_special_field(v));
      if (ec)
        return ec;
      return cxx_support::validate_required_schemas(j, v);
    }
  }
};

template <cxx_support::an_allof T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    error_code ec = try_from_json(j, get_special_field(v));
    if (ec)
      return ec;
    return cxx_support::validate_required_schemas(j, v);
  }
};

template <cxx_support::an_object A>
error_code aggregate_from_json(A& v, const boost::json::value& j) {
  const auto* obj = j.if_object();
  error_code ec;
  if (!obj) {
    ec = parse_error(" object expected, got `{}`", e2str(j.kind()));
    return ec;
  }
  std::bitset unparsed = cxx_support::get_required_bits_mask<A>();

  for (auto&& [key, value] : *obj) {
    // объект может состоять только из additional properties
    if constexpr (fields_count<A>() > 0) {
      if (std::optional<size_t> i = cxx_support::get_field_index<A>(key)) {
        cxx_support::visit_field_index<fields_count<A>()>(
            [&]<size_t I>() {
              if constexpr (cxx_support::get_field_info<I, A>().required)
                unparsed.set(I, false);
              ec = try_from_json(value, cxx_support::get_field<I>(v));
            },
            *i);
        if (ec)
          return ec;
        continue;
      }
    }
    {
      if constexpr (cxx_support::an_object<A> && cxx_support::has_additional_properties<A>) {
        cxx_support::additional_properties_schema_t<A> property;
        ec = try_from_json(value, property);
        if (ec)
          return ec;
        cxx_support::get_additional_properties(v).try_emplace(key, std::move(property));
      } else {
        return parse_error("field `{}` unexpected in type `{}`", std::string_view(key), type_name<A>());
      }
    }
  }
  if (unparsed.any()) {
    ec = parse_error("not all required fields of type `{}` parsed, unparsed: {}", type_name<A>(),
                     unparsed.to_string());
    return ec;
  }
  return {};
}

template <cxx_support::an_object T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    static_assert(cxx_support::valid_generated_object<T>());
    error_code ec = aggregate_from_json(v, j);
    if (ec)
      return ec;
    return cxx_support::validate_required_schemas(j, v);
  }
};

template <cxx_support::a_body T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    // encoding игнорируется, так как для application/json не имеет смысла
    return try_from_json(j, cxx_support::get_special_field(v));
  }
};

template <cxx_support::a_response T>
struct from_json_value<T> {
  static error_code operator()(const boost::json::value& j, T& v) {
    // encoding игнорируется, так как для application/json не имеет смысла
    return try_from_json(j, cxx_support::get_special_field(v));
  }
};

}  // namespace svarg

namespace svarg::cxx_support {

// значение по умолчанию поля из спецификации, разбирается из JSON один раз
template <typename T, constexpr_string Json>
[[nodiscard]] const T& default_value() {
  static const T value = [] {
    T v;
    try_from_json(boost::json::parse(Json.str()), v).rethrow_if_failed();
    return v;
  }();
  return value;
}

}  // namespace svarg::cxx_support
