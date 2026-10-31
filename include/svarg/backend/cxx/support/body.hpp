#pragma once

#include <bitset>
#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/serializer.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/media_types.hpp"
#include "svarg/utils/path_tools.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/nonbody.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/x_www_form_urlencoded.hpp"

// кодирование тел и параметров с `content:` по медиа типу (body_format_of), формат выбирается во время
// компиляции, неподходящая к формату схема - ошибка компиляции:
//  - json: любая схема
//  - form_urlencoded: объект, поля кодируются как параметры query (без JSON кавычек)
//  - raw: строка, число, bool или енам, значение как есть (text/plain "abc", а не "\"abc\"")
//  - multipart: не поддержан

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

// пишет прямо в `out`, без промежуточной строки
inline void serialize_impl(http_body_bytes& out, boost::json::serializer& sr) {
  size_t len = 0;
  out.resize(BOOST_JSON_STACK_BUFFER_SIZE);
  for (;;) {
    boost::json::string_view sv = sr.read(reinterpret_cast<char*>(out.data()) + len, out.size() - len);
    len += sv.size();
    if (sr.done())
      break;
    out.resize(out.size() * 2);
  }
  out.resize(len);
}

template <an_object T>
[[nodiscard]] std::string form_encode(const T& obj) {
  application_x_www_form_urlencoded form;
  FOR_EACH_FIELD_INDEX_OF(T) {
    if (is_field_present<I>(obj))
      form.nonbody_arg(get_field_name<I, T>(), serialize_nonbody_into(get_field<I>(obj)));
  };
  if constexpr (has_additional_properties<T>) {
    for (auto&& [name, value] : get_additional_properties(obj))
      form.nonbody_arg(name, serialize_nonbody_into(value));
  }
  return form.take();
}

// как и для JSON объекта: неизвестные поля - ошибка, если нет additional properties
template <an_object T>
[[nodiscard]] error_code form_decode(std::string_view data, T& out) {
  std::bitset unparsed = get_required_bits_mask<T>();
  error_code ec = visit_query</*WwwUrlEncoded=*/true>(
      data, [&](std::string_view name, std::string_view value) -> error_code {
        if constexpr (fields_count<T>() > 0) {
          if (std::optional<size_t> i = get_field_index<T>(name)) {
            unparsed.set(*i, false);
            return visit_field_index<fields_count<T>()>(
                [&]<size_t I>() { return parse_nonbody_from(value, get_field<I>(out)); }, *i);
          }
        }
        if constexpr (has_additional_properties<T>) {
          additional_properties_schema_t<T> property;
          if (error_code e = parse_nonbody_from(value, property))
            return e;
          get_additional_properties(out).try_emplace(std::string(name), std::move(property));
          return {};
        } else {
          return parse_error("field `{}` unexpected in type `{}`", name, type_name<T>());
        }
      });
  if (ec)
    return ec;
  if (unparsed.any())
    return parse_error("not all required fields of type `{}` parsed, unparsed: {}", type_name<T>(),
                       unparsed.to_string());
  return {};
}

}  // namespace noexport

// параметр с `content:`
template <body_format_e F, typename T>
[[nodiscard]] std::string serialize_value(const T& value) {
  noexport::check_format<F, T>();
  static_assert(F != body_format_e::form_urlencoded, "parameter cannot be form encoded");
  if constexpr (F == body_format_e::json)
    return boost::json::serialize(to_json(value));
  else
    return serialize_nonbody_into(value);
}

template <body_format_e F, typename T>
[[nodiscard]] error_code parse_value(std::string_view data, T& out) {
  noexport::check_format<F, T>();
  static_assert(F != body_format_e::form_urlencoded, "parameter cannot be form encoded");
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

template <body_format_e F, typename T>
[[nodiscard]] http_body_bytes serialize_body(const T& body) {
  noexport::check_format<F, T>();
  http_body_bytes bytes;
  if constexpr (F == body_format_e::json) {
    boost::json::value v = to_json(body);
    unsigned char tmpbuf[256];
    boost::json::serializer sr(boost::json::storage_ptr{}, +tmpbuf, sizeof(tmpbuf));
    sr.reset(&v);
    noexport::serialize_impl(bytes, sr);
  } else {
    std::string s;
    if constexpr (F == body_format_e::form_urlencoded)
      s = noexport::form_encode(body);
    else
      s = serialize_nonbody_into(body);
    bytes.assign(s.begin(), s.end());
  }
  return bytes;
}

template <body_format_e F, typename T>
[[nodiscard]] error_code parse_body(std::string_view data, T& out) {
  noexport::check_format<F, T>();
  if constexpr (F == body_format_e::form_urlencoded)
    return noexport::form_decode(data, out);
  else
    return parse_value<F>(data, out);
}

}  // namespace svarg::cxx_support
