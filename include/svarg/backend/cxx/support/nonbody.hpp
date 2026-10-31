#pragma once

#include <algorithm>
#include <cassert>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/from_chars.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/validate.hpp"
#include "svarg/backend/cxx/support/from_json_value.hpp"
#include "svarg/backend/cxx/support/to_json_value.hpp"

// параметры path/query/header/cookie: значение передаётся как есть, без JSON кодирования
// (строка "abc", а не "\"abc\""). Сначала значение разбирается по представлению (типу),
// затем проверяется по схемам-требованиям и ограничениям, как и при разборе JSON

namespace svarg::cxx_support {

namespace noexport {

template <typename T>
constexpr bool is_nullable_wrapper = !std::is_same_v<svarg::noexport::deoptional_type_t<T>, T>;

// требования проверяются по JSON значению, но строить его нужно только если есть required_schemas,
// для ограничений достаточно разобранного значения
template <typename T>
[[nodiscard]] error_code validate_nonbody(const T& v) {
  if constexpr (required_schemas_t<T>::size == 0)
    return validate_required_schemas(boost::json::value{}, v);
  else
    return validate_required_schemas(to_json(get_special_field(v)), v);
}

}  // namespace noexport

template <typename T>
[[nodiscard]] error_code parse_nonbody_from(std::string_view str, T& out) {
  using U = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<U, std::string>) {
    out = str;
    return {};
  } else if constexpr (std::is_same_v<U, bool>) {
    if (str == "true")
      out = true;
    else if (str == "false")
      out = false;
    else
      return parse_error("cannot parse boolean from `{}`", str);
    return {};
  } else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>) {
    if (!try_from_chars(str, out))
      return parse_error("cannot parse `{}` from `{}`", type_name<U>(), str);
    return {};
  } else if constexpr (std::is_same_v<U, number>) {
    int64_t i;
    uint64_t u;
    double d;
    if (try_from_chars(str, i))
      out = i;
    else if (try_from_chars(str, u))
      out = u;
    else if (try_from_chars(str, d))
      out = d;
    else
      return parse_error("cannot parse number from `{}`", str);
    return {};
  } else if constexpr (std::is_same_v<U, null>) {
    if (str != "null")
      return parse_error("expected null, got `{}`", str);
    return {};
  } else if constexpr (noexport::is_nullable_wrapper<U>) {
    return parse_nonbody_from(str, out.emplace());
  } else if constexpr (a_string_enum<U>) {
    auto it = std::ranges::find(U::values, str);
    if (it == U::values.end())
      return validate_error("string `{}` cannot be value of enum {}", str, type_name<U>());
    // порядок значений енама совпадает с порядком в массиве
    get_special_field(out) = typename U::values_e(std::distance(U::values.begin(), it));
    return {};
  } else if constexpr (an_enum<U>) {
    std::decay_t<decltype(get_special_field(out))> v;
    error_code ec = parse_nonbody_from(str, v);
    if (ec)
      return ec;
    if (!std::ranges::contains(U::values, v))
      return validate_error("`{}` cannot represent enum {}", str, type_name<U>());
    get_special_field(out) = std::move(v);
    return {};
  } else if constexpr (an_fundamental<U>) {
    error_code ec = parse_nonbody_from(str, get_special_field(out));
    if (ec)
      return ec;
    return noexport::validate_nonbody(out);
  } else {
    // объекты, массивы и т.д. пока как JSON (style/explode не поддерживаются)
    std::error_code jec;
    boost::json::value v = boost::json::parse(str, jec);
    if (jec)
      return parse_error("cannot parse `{}` as JSON: {}", str, jec.message());
    return try_from_json(v, out);
  }
}

// обратное к parse_nonbody_from. Опциональное значение должно присутствовать
template <typename T>
[[nodiscard]] std::string serialize_nonbody_into(const T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    return value;
  } else if constexpr (std::is_same_v<T, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
    return std::format("{}", value);
  } else if constexpr (std::is_same_v<T, number>) {
    return std::visit([](auto x) { return std::format("{}", x); }, value.data);
  } else if constexpr (std::is_same_v<T, null>) {
    return "null";
  } else if constexpr (noexport::is_nullable_wrapper<T>) {
    assert(value.has_value());
    return serialize_nonbody_into(*value);
  } else if constexpr (a_string_enum<T>) {
    return std::string(T::values[std::to_underlying(get_special_field(value))]);
  } else if constexpr (an_enum<T>) {
    return serialize_nonbody_into(get_special_field(value));
  } else if constexpr (an_fundamental<T>) {
    // как и to_json, отправлять значение не удовлетворяющее схеме нельзя
    noexport::validate_nonbody(value).rethrow_if_failed();
    return serialize_nonbody_into(get_special_field(value));
  } else {
    return boost::json::serialize(to_json(value));
  }
}

}  // namespace svarg::cxx_support
