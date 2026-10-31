#pragma once

#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/media_types.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/nonbody.hpp"

// кодирование одного значения по медиа типу (body_format_of): параметр с `content:`, поле
// form-urlencoded тела, часть multipart. Неподходящая к формату схема - ошибка компиляции

namespace svarg::cxx_support {

// значение, которое кодируется текстом без JSON (см. parse_nonbody_from)
template <typename T>
constexpr bool is_plain_value = [] {
  using U = std::remove_cv_t<T>;
  if constexpr (noexport::is_nullable_wrapper<U>)
    return is_plain_value<svarg::noexport::deoptional_type_t<U>>;
  else
    return std::is_same_v<U, std::string> || std::is_arithmetic_v<U> || std::is_same_v<U, number> ||
           std::is_same_v<U, null> || a_string_enum<U> || an_enum<U> || an_fundamental<U>;
}();

namespace noexport {

template <body_format_e F, typename T>
consteval void check_format() {
  if constexpr (F == body_format_e::raw)
    static_assert(is_plain_value<T>,
                  "not JSON media type (text/plain, application/octet-stream...) requires string, "
                  "number, boolean or enum schema");
  else if constexpr (F == body_format_e::form_urlencoded)
    static_assert(an_object<T>, "application/x-www-form-urlencoded requires object schema");
  else if constexpr (F == body_format_e::multipart)
    static_assert(false, "multipart media types are not supported yet");
}

}  // namespace noexport

// формат значения, если медиа тип не указан (encoding без contentType): текст для простых значений,
// JSON для остальных. Как и в OpenAPI (text/plain и application/json по умолчанию)
template <typename T>
constexpr body_format_e default_format = is_plain_value<T> ? body_format_e::raw : body_format_e::json;

// медиа тип по умолчанию, см. default_format
template <typename T>
constexpr std::string_view default_media_type = is_plain_value<T> ? "text/plain" : "application/json";

// параметр с `content:`
template <body_format_e F, typename T>
[[nodiscard]] std::string serialize_value(const T& value) {
  noexport::check_format<F, T>();
  static_assert(F == body_format_e::json || F == body_format_e::raw, "value cannot be form or multipart");
  if constexpr (F == body_format_e::json)
    return boost::json::serialize(to_json(value));
  else
    return serialize_nonbody_into(value);
}

template <body_format_e F, typename T>
[[nodiscard]] error_code parse_value(std::string_view data, T& out) {
  noexport::check_format<F, T>();
  static_assert(F == body_format_e::json || F == body_format_e::raw, "value cannot be form or multipart");
  if constexpr (F == body_format_e::json) {
    std::error_code jec;
    boost::json::value v = boost::json::parse(data, jec);
    if (jec)
      return parse_error("cannot parse JSON: {}", jec.message());
    return try_from_json(v, out);
  } else {
    return parse_nonbody_from(data, out);
  }
}

}  // namespace svarg::cxx_support
