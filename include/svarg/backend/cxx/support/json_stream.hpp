#pragma once

#include <boost/json/basic_parser_impl.hpp>
#include <omo/token_feeder.hpp>

#include "svarg/backend/cxx/support/from_tokens.hpp"

namespace svarg::cxx_support {

// потоковый разбор JSON текста (json_codec): токены сразу разбираются в `out` (from_tokens) с проверками
// требований схемы
template <typename T>
struct json_stream_parser {
 private:
  boost::json::basic_parser<omo::boostjson_token_feeder> p;

 public:
  json_stream_parser(std::string_view /*content_type*/, T& out)
      : p(boost::json::parse_options{}, make_make_parser_for(out), std::span<unsigned char>{}) {
  }

  json_stream_parser(json_stream_parser&&) = delete;
  void operator=(json_stream_parser&&) = delete;

  // `last` - кусок последний. Ошибка - исключение (parse_error, validate_error)
  void feed(std::string_view part, bool last) {
    boost::system::error_code ec;
    size_t n;
    try {
      n = p.write_some(!last, part.data(), part.size(), ec);
    } catch (omo::parse_error& e) {  // неожиданный токен
      throw svarg::parse_error("cannot parse JSON: {}", e.what());
    }
    if (ec)
      throw svarg::parse_error("cannot parse JSON: {}", ec.message());
    if (n != part.size())
      throw svarg::parse_error("cannot parse JSON: extra data after value");
    if (last && !p.handler().is_done())
      throw svarg::parse_error("cannot parse JSON: unexpected end of data");
  }
};

}  // namespace svarg::cxx_support

namespace svarg {

// JSON текст в T через токены, без boost::json::value, с проверками схемы. Ошибка - исключение (parse_error,
// validate_error)
template <typename T>
[[nodiscard]] T from_json_string(std::string_view str) {
  T value;
  cxx_support::json_stream_parser<T>("application/json", value).feed(str, /*last=*/true);
  return value;
}

}  // namespace svarg
