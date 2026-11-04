#pragma once

#include <string>
#include <string_view>
#include <type_traits>

#include "svarg/errors.hpp"
#include "svarg/utils/constexpr_string.hpp"
#include "svarg/utils/media_types.hpp"
#include "svarg/backend/cxx/support/http_base.hpp"

// КОДЕКИ
//
// Кодек переводит значение схемы в байты медиа типа и обратно. Все форматы - тела запросов и ответов,
// части multipart, поля form-urlencoded, параметры с `content:` - кодируются через один интерфейс:
//
//   struct my_codec {
//     // `media_type` - без параметров. В `params` можно добавить параметры хедера content-type (например
//     // boundary у multipart). Данные дописываются в конец `out`
//     template <typename T>
//     static void serialize(const T& value, std::string_view media_type, content_type_params& params,
//                           http_body_bytes& out);
//     // `content_type` - значение хедера content-type целиком, с параметрами
//     template <typename T>
//     static error_code parse(std::string_view content_type, std::string_view data, T& out);
//   };
//
// Встроенные (выбираются по body_format_of медиа типа):
//   json_codec      - application/json и любой +json
//   form_codec      - application/x-www-form-urlencoded, объект
//   multipart_codec - multipart/*, объект
//   raw_codec       - остальное: строка, число, bool или енам как есть
//
// Свой кодек - специализация media_codec для медиа типа:
//
//   template <>
//   struct svarg::cxx_support::media_codec<"application/xml"> : my_xml_codec {};
//
// Медиа тип, который кодируется как другой, - специализация media_type_base для медиа типа или суффикса
// структурного синтаксиса ("+xml"):
//
//   template <>
//   struct svarg::cxx_support::media_type_base<"+xml"> {
//     static constexpr std::string_view value = "application/xml";
//   };
//
// Встроенные: +json -> application/json, +xml -> application/xml.
// Порядок выбора: media_codec<медиа тип>, кодек базового медиа типа (media_type_base<медиа тип>, затем
// media_type_base<суффикс>), встроенный. Медиа типы в нижнем регистре. Специализации должны быть видны во
// всех сгенерированных файлах (иначе разные единицы трансляции выберут разные кодеки), поэтому заголовок с
// ними подключается в начало каждого файла опцией генератора: svarg --file-prefix '#include "my_codecs.hpp"'

namespace svarg::cxx_support {

using hidi::http_body_bytes;

template <constexpr_string MediaType>
struct media_codec;  // не определён: своего кодека нет

template <constexpr_string MediaType>
struct media_type_base;  // не определён: медиа тип кодируется сам по себе

template <>
struct media_type_base<"+json"> {
  static constexpr std::string_view value = "application/json";
};

template <>
struct media_type_base<"+xml"> {
  static constexpr std::string_view value = "application/xml";
};

struct json_codec;
struct raw_codec;
struct form_codec;
struct multipart_codec;

inline void append(http_body_bytes& out, std::string_view s) {
  out.insert(out.end(), s.begin(), s.end());
}

namespace noexport {

template <constexpr_string M>
constexpr bool has_media_codec = requires { sizeof(media_codec<M>); };

template <constexpr_string M>
constexpr bool has_media_type_base = requires { media_type_base<M>::value; };

// "application/vnd.x+xml" -> "+xml"
consteval std::string_view media_suffix(std::string_view media_type) {
  size_t plus = media_type.rfind('+');
  if (plus == std::string_view::npos || media_type.find('/') > plus)
    return {};
  return media_type.substr(plus);
}

// базовый медиа тип `M` или пусто
template <constexpr_string M>
consteval std::string_view base_media_type() {
  constexpr std::string_view suffix = media_suffix(M.str());
  if constexpr (has_media_type_base<M>)
    return media_type_base<M>::value;
  else if constexpr (!suffix.empty() && has_media_type_base<constexpr_string<suffix.size()>(suffix)>)
    return media_type_base<constexpr_string<suffix.size()>(suffix)>::value;
  else
    return {};
}

template <constexpr_string M>
consteval auto select_codec() {
  constexpr std::string_view base = base_media_type<M>();
  if constexpr (has_media_codec<M>) {
    return std::type_identity<media_codec<M>>{};
  } else if constexpr (!base.empty()) {
    static_assert(base != M.str(), "media_type_base maps media type to itself");
    return select_codec<constexpr_string<base.size()>(base)>();
  } else {
    constexpr body_format_e f = body_format_of(M.str());
    if constexpr (f == body_format_e::json)
      return std::type_identity<json_codec>{};
    else if constexpr (f == body_format_e::form_urlencoded)
      return std::type_identity<form_codec>{};
    else if constexpr (f == body_format_e::multipart)
      return std::type_identity<multipart_codec>{};
    else
      return std::type_identity<raw_codec>{};
  }
}

}  // namespace noexport

// медиа типы в сгенерированном коде - constexpr std::string_view, а параметром шаблона он быть не может,
// поэтому медиа тип передаётся как constexpr_string: constexpr_string<sv.size()>(sv)
template <constexpr_string MediaType>
using codec_for = typename decltype(noexport::select_codec<MediaType>())::type;

// значение (параметр с `content:`, поле form) в строку
template <constexpr_string MediaType, typename T>
[[nodiscard]] std::string encode_value(const T& value) {
  content_type_params unused;
  http_body_bytes bytes;
  codec_for<MediaType>::serialize(value, MediaType.str(), unused, bytes);
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

template <constexpr_string MediaType, typename T>
[[nodiscard]] error_code decode_value(std::string_view data, T& out) {
  return codec_for<MediaType>::parse(MediaType.str(), data, out);
}

}  // namespace svarg::cxx_support
