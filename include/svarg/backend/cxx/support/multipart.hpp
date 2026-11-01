#pragma once

#include <bitset>
#include <random>
#include <string>
#include <string_view>

#include "svarg/errors.hpp"
#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/utils/media_types.hpp"
#include "svarg/utils/strcmp.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/value_format.hpp"

// multipart тела (RFC 2046): каждое свойство объекта - часть.
//  - multipart/form-data: часть сопоставляется со свойством по имени из Content-Disposition
//  - остальные (related, mixed...): по content type части среди ещё не заполненных свойств, при
//    нескольких подходящих - по порядку. Части с одинаковым типом (несколько ngap в 3GPP) различаются
//    хедерами частей (Content-Id), на которые ссылается JSON часть
// Медиа тип части - encoding.contentType свойства, по умолчанию text/plain для простых значений и
// application/json для остальных

namespace svarg::cxx_support {

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

namespace noexport {

[[nodiscard]] inline std::string make_multipart_boundary() {
  // RFC 2046: до 70 символов из bchars, не должен встречаться в данных. 24 случайных символа
  static constexpr std::string_view alphabet = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  thread_local std::mt19937_64 gen{std::random_device{}()};
  std::string b = "svarg-";
  std::uniform_int_distribution<size_t> dist(0, alphabet.size() - 1);
  for (int i = 0; i < 24; ++i)
    b += alphabet[dist(gen)];
  return b;
}

template <typename H>
void serialize_part_headers(const H& h, std::string& out) {
  FOR_EACH_FIELD_INDEX_OF(H) {
    if (is_field_present<I>(h))
      out += std::format("{}: {}\r\n", get_field_name<I, H>(), serialize_nonbody_field<I>(h));
  };
}

// хедеры части после разбора: имена в нижнем регистре
struct part_headers_view {
  std::vector<std::pair<std::string, std::string_view>> headers;

  [[nodiscard]] std::string_view get(std::string_view lowered_name) const noexcept {
    for (auto& [n, v] : headers)
      if (n == lowered_name)
        return v;
    return {};
  }
};

template <typename H>
[[nodiscard]] error_code parse_part_headers(const part_headers_view& view, H& out) {
  std::bitset unparsed = get_required_bits_mask<H>();
  for (auto& [name, value] : view.headers) {
    std::optional index = get_field_index<H>(name);
    if (!index)
      continue;
    unparsed.set(*index, false);
    error_code ec = visit_field_index<fields_count<H>()>(
        [&]<size_t I>() { return parse_nonbody_field<I>(value, out); }, *index);
    if (ec)
      return ec;
  }
  if (unparsed.any())
    return parse_error("not all required part headers are present, unparsed: {}", unparsed.to_string());
  return {};
}

// медиа тип свойства I: encoding.contentType или по умолчанию
template <an_object T, size_t I>
consteval std::string_view part_media_type() {
  if constexpr (constexpr std::string_view enc = get_field_encoding<I, T>(); !enc.empty())
    return enc;
  using F = svarg::noexport::deoptional_type_t<decltype(get_field<I>(std::declval<T&>()))>;
  return default_media_type<typename part_value<F>::type>;
}

template <an_object T, size_t I>
void serialize_part(const T& obj, std::string_view boundary, bool form_data, std::string& out) {
  constexpr std::string_view media_type = part_media_type<T, I>();
  const auto& field = get_field<I>(obj);
  const auto& f = [&]() -> decltype(auto) {
    if constexpr (requires { field.has_value(); })
      return *field;
    else
      return field;
  }();
  out += std::format("--{}\r\n", boundary);
  if (form_data) {
    // имя свойства в кавычках, `"` и `\` экранируются
    out += "Content-Disposition: form-data; name=\"";
    for (char c : get_field_name<I, T>()) {
      if (c == '"' || c == '\\')
        out += '\\';
      out += c;
    }
    out += "\"\r\n";
  }
  out += std::format("Content-Type: {}\r\n", media_type);
  if constexpr (is_part<std::remove_cvref_t<decltype(f)>>) {
    serialize_part_headers(f.headers, out);
    out += "\r\n";
    out += serialize_value<body_format_of(media_type)>(f.value);
  } else {
    out += "\r\n";
    out += serialize_value<body_format_of(media_type)>(f);
  }
  out += "\r\n";
}

template <an_object T, size_t I>
[[nodiscard]] error_code parse_part(const part_headers_view& headers, std::string_view data, T& obj) {
  constexpr std::string_view media_type = part_media_type<T, I>();
  auto& field = get_field<I>(obj);
  auto& f = [&]() -> decltype(auto) {
    if constexpr (requires { field.emplace(); })
      return field.emplace();
    else
      return (field);
  }();
  if constexpr (is_part<std::remove_cvref_t<decltype(f)>>) {
    if (error_code ec = parse_part_headers(headers, f.headers))
      return ec;
    return parse_value<body_format_of(media_type)>(data, f.value);
  } else {
    return parse_value<body_format_of(media_type)>(data, f);
  }
}

// вызывает `on_part(headers, data)` для каждой части `body`
[[nodiscard]] inline error_code visit_multipart(std::string_view body, std::string_view boundary,
                                                auto&& on_part) {
  if (boundary.empty())
    return parse_error("multipart content type without boundary");
  std::string delimiter = std::format("--{}", boundary);
  // преамбула до первого разделителя игнорируется
  size_t pos = body.starts_with(delimiter) ? 0 : body.find("\r\n" + delimiter);
  if (pos == std::string_view::npos)
    return parse_error("multipart boundary `{}` not found", boundary);
  body.remove_prefix(pos == 0 ? delimiter.size() : pos + 2 + delimiter.size());
  std::string next_delimiter = "\r\n" + delimiter;
  for (;;) {
    if (body.starts_with("--"))
      return {};  // закрывающий разделитель, эпилог игнорируется
    // после разделителя допустимы пробелы (transport padding), затем CRLF
    while (body.starts_with(' ') || body.starts_with('\t'))
      body.remove_prefix(1);
    if (!body.starts_with("\r\n"))
      return parse_error("expected CRLF after multipart boundary");
    body.remove_prefix(2);
    size_t end = body.find(next_delimiter);
    if (end == std::string_view::npos)
      return parse_error("multipart body is not terminated by boundary `{}`", boundary);
    std::string_view part = body.substr(0, end);
    body.remove_prefix(end + next_delimiter.size());

    part_headers_view headers;
    // хедеры до пустой строки, часть без хедеров начинается сразу с CRLF
    while (!part.starts_with("\r\n")) {
      size_t line_end = part.find("\r\n");
      if (line_end == std::string_view::npos)
        return parse_error("multipart part headers are not terminated");
      std::string_view line = part.substr(0, line_end);
      part.remove_prefix(line_end + 2);
      size_t colon = line.find(':');
      if (colon == std::string_view::npos)
        return parse_error("invalid multipart part header `{}`", line);
      std::string_view value = line.substr(colon + 1);
      while (value.starts_with(' ') || value.starts_with('\t'))
        value.remove_prefix(1);
      while (value.ends_with(' ') || value.ends_with('\t'))
        value.remove_suffix(1);
      headers.headers.emplace_back(lowered(line.substr(0, colon)), value);
    }
    part.remove_prefix(2);
    if (error_code ec = on_part(headers, part))
      return ec;
  }
}

}  // namespace noexport

// `params` получает boundary (и type корневой части для multipart/related)
template <an_object T>
[[nodiscard]] std::string multipart_encode(const T& obj, std::string_view media_type,
                                           content_type_params& params) {
  static_assert(!has_additional_properties<T>, "multipart with additionalProperties is not supported");
  std::string boundary = noexport::make_multipart_boundary();
  bool form_data = media_type == "multipart/form-data";
  std::string out;
  std::string_view root_type;
  FOR_EACH_FIELD_INDEX_OF(T) {
    if (is_field_present<I>(obj)) {
      if (root_type.empty())
        root_type = noexport::part_media_type<T, I>();
      noexport::serialize_part<T, I>(obj, boundary, form_data, out);
    }
  };
  out += std::format("--{}--\r\n", boundary);
  // RFC 2387: у multipart/related обязателен type - медиа тип корневой (первой) части
  if (media_type == "multipart/related" && !root_type.empty())
    params.try_emplace("type", root_type);
  params.insert_or_assign("boundary", std::move(boundary));
  return out;
}

// `content_type` - значение хедера целиком, с boundary
template <an_object T>
[[nodiscard]] error_code multipart_decode(std::string_view content_type, std::string_view data, T& out) {
  static_assert(!has_additional_properties<T>, "multipart with additionalProperties is not supported");
  content_type_params params;
  std::string_view media_type;
  try {
    media_type = media_type_from_hdr_value(content_type, params);
  } catch (std::exception& e) {
    return parse_error("invalid content type `{}`: {}", content_type, e.what());
  }
  bool form_data = is_equal_ci(media_type, "multipart/form-data");
  auto it = params.find("boundary");
  std::bitset<fields_count<T>()> filled;

  error_code ec = noexport::visit_multipart(
      data, it == params.end() ? std::string_view{} : std::string_view(it->second),
      [&](const noexport::part_headers_view& headers, std::string_view part) -> error_code {
        std::optional<size_t> index;
        if (form_data) {
          // Content-Disposition имеет тот же синтаксис параметров, что и Content-Type
          content_type_params disposition;
          std::string_view kind;
          try {
            kind = media_type_from_hdr_value(headers.get("content-disposition"), disposition);
          } catch (std::exception& e) {
            return parse_error("invalid Content-Disposition: {}", e.what());
          }
          auto name = disposition.find("name");
          if (!is_equal_ci(kind, "form-data") || name == disposition.end())
            return parse_error("form-data part without Content-Disposition name");
          index = get_field_index<T>(name->second);
          if (!index)
            return parse_error("unexpected form-data part `{}` in `{}`", name->second, type_name<T>());
        } else {
          // RFC 2046: без Content-Type часть - text/plain
          std::string_view part_type = headers.get("content-type");
          if (part_type.empty())
            part_type = "text/plain";
          if (is_wildcard_content_type(part_type))
            return parse_error("multipart part content type `{}` is a range", part_type);
          FOR_EACH_FIELD_INDEX_OF(T) {
            if (!index && !filled.test(I) && match_content_type(noexport::part_media_type<T, I>(), part_type))
              index = I;
          };
          if (!index)
            return parse_error("multipart part `{}` does not match any free property of `{}`", part_type,
                               type_name<T>());
        }
        if (filled.test(*index))
          return parse_error("duplicate multipart part for property `{}`", T::fields[*index].name);
        filled.set(*index);
        return visit_field_index<fields_count<T>()>(
            [&]<size_t I>() { return noexport::parse_part<T, I>(headers, part, out); }, *index);
      });
  if (ec)
    return ec;
  std::bitset unparsed = get_required_bits_mask<T>() & ~filled;
  if (unparsed.any())
    return parse_error("not all required parts of `{}` are present, unparsed: {}", type_name<T>(),
                       unparsed.to_string());
  return {};
}

}  // namespace svarg::cxx_support

namespace svarg {

template <typename T, typename H>
struct from_json_value<cxx_support::part<T, H>> {
  static error_code operator()(const boost::json::value& j, cxx_support::part<T, H>& v) {
    return try_from_json(j, v.value);
  }
};

template <typename T, typename H>
struct to_json_value<cxx_support::part<T, H>> {
  static boost::json::value operator()(const cxx_support::part<T, H>& v) {
    return to_json(v.value);
  }
};

}  // namespace svarg
