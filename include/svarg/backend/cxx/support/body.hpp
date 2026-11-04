#pragma once

#include <bitset>
#include <string>
#include <string_view>

#include "svarg/errors.hpp"
#include "svarg/utils/media_types.hpp"
#include "svarg/utils/path_tools.hpp"
#include "svarg/backend/cxx/support/codec.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/value_format.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/x_www_form_urlencoded.hpp"
#include "svarg/backend/cxx/support/multipart.hpp"

// тела запросов и ответов: кодек выбирается по медиа типу тела (codec.hpp)

namespace svarg::cxx_support {

// application/x-www-form-urlencoded: объект, поле кодируется кодеком своего медиа типа
// (encoding.contentType), по умолчанию как параметр query (без JSON кавычек), если это простое значение,
// иначе JSON. Как и для JSON объекта неизвестные поля - ошибка, если нет additional properties
struct form_codec {
  template <typename T>
  static void serialize(const T& obj, std::string_view, content_type_params&, http_body_bytes& out) {
    static_assert(an_object<T>, "application/x-www-form-urlencoded requires object schema");
    application_x_www_form_urlencoded form;
    FOR_EACH_FIELD_INDEX_OF(T) {
      if (is_field_present<I>(obj))
        form.arg<field_media_type_v<T, I>>(get_field_name<I, T>(), get_field<I>(obj));
    };
    if constexpr (has_additional_properties<T>) {
      using V = additional_properties_schema_t<T>;
      for (auto&& [name, value] : get_additional_properties(obj))
        form.arg<default_media_type_v<V>>(name, value);
    }
    append(out, form.take());
  }

  template <typename T>
  [[nodiscard]] static error_code parse(std::string_view, std::string_view data, T& out) {
    static_assert(an_object<T>, "application/x-www-form-urlencoded requires object schema");
    std::bitset unparsed = get_required_bits_mask<T>();
    error_code ec = visit_query</*WwwUrlEncoded=*/true>(
        data, [&](std::string_view name, std::string_view value) -> error_code {
          if constexpr (fields_count<T>() > 0) {
            if (std::optional<size_t> i = get_field_index<T>(name)) {
              unparsed.set(*i, false);
              return visit_field_index<fields_count<T>()>(
                  [&]<size_t I>() { return decode_value<field_media_type_v<T, I>>(value, get_field<I>(out)); },
                  *i);
            }
          }
          if constexpr (has_additional_properties<T>) {
            using V = additional_properties_schema_t<T>;
            V property;
            if (error_code e = decode_value<default_media_type_v<V>>(value, property))
              return e;
            get_additional_properties(out).try_emplace(std::string(name), std::move(property));
            return {};
          } else if constexpr (forbids_additional_properties<T>) {
            return parse_error("field `{}` unexpected in type `{}`", name, type_name<T>());
          } else {
            return {};
          }
        });
    if (ec)
      return ec;
    if (unparsed.any())
      return parse_error("not all required fields of type `{}` parsed, unparsed: {}", type_name<T>(),
                         unparsed.to_string());
    return {};
  }
};

// тело ответа потокового медиа типа (--stream-content-type): данных в структуре нет. Сервер отправляет их
// ctx.stream_response(rsp.status, make_response_headers(rsp), body), клиент получает в колбэк send.
// Какой медиа тип пришёл (или отправляется), видно по типу тела, если тел у ответа несколько
struct stream_body {
  bool operator==(const stream_body&) const = default;
};

// тело медиа типа `MediaType`. `params` - параметры хедера content-type, кодек может их дополнить
// (boundary у multipart)
template <constexpr_string MediaType, typename T>
[[nodiscard]] http_body_bytes serialize_body(const T& body, content_type_params& params) {
  if constexpr (std::is_same_v<T, stream_body>) {
    throw serialize_error(
        "body of media type `{}` is a stream: send it by ctx.stream_response(rsp.status, "
        "make_response_headers(rsp), body)",
        MediaType.str());
  } else {
    http_body_bytes bytes;
    codec_for<MediaType>::serialize(body, MediaType.str(), params, bytes);
    return bytes;
  }
}

// `content_type` - значение хедера content-type целиком (с параметрами)
template <constexpr_string MediaType, typename T>
[[nodiscard]] error_code parse_body(std::string_view content_type, std::string_view data, T& out) {
  if constexpr (std::is_same_v<T, stream_body>)
    return {};  // данные потока отданы в колбэк
  else
    return codec_for<MediaType>::parse(content_type, data, out);
}

}  // namespace svarg::cxx_support
