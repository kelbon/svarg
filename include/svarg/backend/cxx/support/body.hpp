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
#include "svarg/backend/cxx/support/value_format.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/x_www_form_urlencoded.hpp"
#include "svarg/backend/cxx/support/multipart.hpp"

// кодирование тел по медиа типу (body_format_of), формат выбирается во время компиляции, неподходящая к
// формату схема - ошибка компиляции:
//  - json: любая схема
//  - form_urlencoded: объект, поле кодируется по своему encoding.contentType, по умолчанию как
//    параметр query (без JSON кавычек), если это простое значение, иначе JSON
//  - raw: строка, число, bool или енам, значение как есть (text/plain "abc", а не "\"abc\"")
//  - multipart: объект, каждое свойство - часть (см. multipart.hpp)

namespace svarg::cxx_support {

namespace noexport {

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

template <an_object T, size_t I>
using field_value_t = svarg::noexport::deoptional_type_t<decltype(get_field<I>(std::declval<T&>()))>;

// медиа тип поля form/multipart тела: encoding.contentType или по умолчанию
template <an_object T, size_t I>
consteval std::string_view field_media_type() {
  if constexpr (has_encodings<T>) {
    constexpr std::string_view enc = T::encoding_for(get_field_name<I, T>());
    if constexpr (!enc.empty())
      return enc;
  }
  return default_media_type<field_value_t<T, I>>;
}

template <an_object T>
[[nodiscard]] std::string form_encode(const T& obj) {
  application_x_www_form_urlencoded form;
  FOR_EACH_FIELD_INDEX_OF(T) {
    if (is_field_present<I>(obj))
      form.arg<body_format_of(field_media_type<T, I>())>(get_field_name<I, T>(), get_field<I>(obj));
  };
  if constexpr (has_additional_properties<T>) {
    using V = additional_properties_schema_t<T>;
    for (auto&& [name, value] : get_additional_properties(obj))
      form.arg<default_format<V>>(name, value);
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
                [&]<size_t I>() {
                  return parse_value<body_format_of(field_media_type<T, I>())>(value, get_field<I>(out));
                },
                *i);
          }
        }
        if constexpr (has_additional_properties<T>) {
          using V = additional_properties_schema_t<T>;
          V property;
          if (error_code e = parse_value<default_format<V>>(value, property))
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

// `media_type` - медиа тип тела без параметров, `params` - параметры хедера content-type (multipart
// добавляет boundary)
template <body_format_e F, typename T>
[[nodiscard]] http_body_bytes serialize_body(const T& body, std::string_view media_type,
                                             content_type_params& params) {
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
    else if constexpr (F == body_format_e::multipart)
      s = multipart_encode(body, media_type, params);
    else
      s = serialize_nonbody_into(body);
    bytes.assign(s.begin(), s.end());
  }
  return bytes;
}

// `content_type` - значение хедера content-type целиком (с параметрами)
template <body_format_e F, typename T>
[[nodiscard]] error_code parse_body(std::string_view content_type, std::string_view data, T& out) {
  noexport::check_format<F, T>();
  if constexpr (F == body_format_e::form_urlencoded)
    return noexport::form_decode(data, out);
  else if constexpr (F == body_format_e::multipart)
    return multipart_decode(content_type, data, out);
  else
    return parse_value<F>(data, out);
}

}  // namespace svarg::cxx_support
