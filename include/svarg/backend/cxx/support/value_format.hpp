#pragma once

#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/serializer.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/media_types.hpp"
#include "svarg/backend/cxx/support/codec.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/nonbody.hpp"

// встроенные кодеки значений (json_codec, raw_codec) и медиа типы свойств: параметр с `content:`, поле
// form-urlencoded тела, часть multipart

namespace svarg::cxx_support {

// значение, которое кодируется текстом без JSON (см. parse_nonbody_from)
template <typename T>
constexpr bool is_plain_value = [] {
  using U = std::remove_cv_t<T>;
  if constexpr (noexport::is_nullable_wrapper<U>)
    return is_plain_value<svarg::noexport::deoptional_type_t<U>>;
  else
    return std::is_same_v<U, std::string> || std::is_arithmetic_v<U> || std::is_same_v<U, number> ||
           std::is_same_v<U, null> || a_string_enum<U> || an_open_string_enum<U> || an_enum<U> ||
           an_fundamental<U>;
}();

// свойство multipart тела, у которого в encoding описаны хедеры части. Вне multipart (JSON) это просто
// `value`, хедеры игнорируются
template <typename T, typename Headers>
struct part {
  Headers headers;
  T value;

  bool operator==(const part&) const = default;
};

template <typename T>
constexpr bool is_part = false;

template <typename T, typename H>
constexpr bool is_part<part<T, H>> = true;

template <typename T>
struct part_value : std::type_identity<T> {};

template <typename T, typename H>
struct part_value<part<T, H>> : std::type_identity<T> {};

// медиа тип значения, если он не указан (encoding без contentType): text/plain для простых значений,
// application/json для остальных, как и в OpenAPI
template <typename T>
constexpr std::string_view default_media_type() {
  return is_plain_value<T> ? "text/plain" : "application/json";
}

// медиа тип свойства I объекта T (поле form/multipart тела): encoding.contentType или по умолчанию
template <typename T, size_t I>
constexpr std::string_view field_media_type() {
  if constexpr (constexpr std::string_view enc = get_field_encoding<I, T>(); !enc.empty()) {
    return enc;
  } else {
    using F = svarg::noexport::deoptional_type_t<decltype(get_field<I>(std::declval<T&>()))>;
    return default_media_type<typename part_value<F>::type>();
  }
}

namespace noexport {

// пишет прямо в `out`, без промежуточной строки
inline void serialize_json_into(http_body_bytes& out, const boost::json::value& v) {
  unsigned char tmpbuf[256];
  boost::json::serializer sr(boost::json::storage_ptr{}, +tmpbuf, sizeof(tmpbuf));
  sr.reset(&v);
  size_t len = out.size();
  out.resize(len + BOOST_JSON_STACK_BUFFER_SIZE, hidi::uninitialized_byte);
  for (;;) {
    boost::json::string_view sv = sr.read(reinterpret_cast<char*>(out.data()) + len, out.size() - len);
    len += sv.size();
    if (sr.done())
      break;
    out.resize(out.size() * 2, hidi::uninitialized_byte);
  }
  out.resize(len);
}

}  // namespace noexport

struct json_codec {
  template <typename T>
  static void serialize(const T& value, std::string_view, content_type_params&, http_body_bytes& out) {
    noexport::serialize_json_into(out, to_json(value));
  }
  template <typename T>
  [[nodiscard]] static error_code parse(std::string_view, std::string_view data, T& out) {
    std::error_code jec;
    boost::json::value v = boost::json::parse(data, jec);
    if (jec)
      return parse_error("cannot parse JSON: {}", jec.message());
    return try_from_json(v, out);
  }
};

// значение как есть (text/plain "abc", а не "\"abc\""), для медиа типов без своего кодека
struct raw_codec {
  template <typename T>
  static void serialize(const T& value, std::string_view, content_type_params&, http_body_bytes& out) {
    check<T>();
    append(out, serialize_nonbody_into(value));
  }
  template <typename T>
  [[nodiscard]] static error_code parse(std::string_view, std::string_view data, T& out) {
    check<T>();
    return parse_nonbody_from(data, out);
  }

 private:
  template <typename T>
  static consteval void check() {
    static_assert(
        is_plain_value<T>,
        "media type without codec (text/plain, application/octet-stream...) requires string, number, "
        "boolean or enum schema. For other schemas specialize svarg::cxx_support::media_codec "
        "for this media type (see codec.hpp)");
  }
};

// параметр с `content:` (encoding поля не пуст) кодируется его кодеком, остальные - как текст
template <size_t FieldIndex, typename T>
std::string serialize_nonbody_field(const T& f, std::index_sequence<FieldIndex> = {}) {
  if constexpr (!get_field_encoding<FieldIndex, T>().empty())
    return encode_value<&get_field_encoding<FieldIndex, T>>(get_field<FieldIndex>(f));
  else
    return serialize_nonbody_into(get_field<FieldIndex>(f));
}

// обратное к serialize_nonbody_field
template <size_t FieldIndex, typename T>
[[nodiscard]] error_code parse_nonbody_field(std::string_view value, T& f) {
  if constexpr (!get_field_encoding<FieldIndex, T>().empty())
    return decode_value<&get_field_encoding<FieldIndex, T>>(value, get_field<FieldIndex>(f));
  else
    return parse_nonbody_from(value, get_field<FieldIndex>(f));
}

}  // namespace svarg::cxx_support
