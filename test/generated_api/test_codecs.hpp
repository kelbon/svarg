#pragma once

// пользовательские кодеки для test_generated, подключаются в каждый сгенерированный файл через
// svarg --file-prefix (см. test/CMakeLists.txt)

#include <algorithm>
#include <cctype>

#include "svarg/backend/cxx/support/codec.hpp"
#include "svarg/backend/cxx/support/value_format.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/hooks.hpp"

namespace test_codecs {

// токен, который хук before_send добавляет в незаполненную авторизацию
inline std::optional<std::string> current_token;

using namespace svarg::cxx_support;

// "a=x;n=1" для объектов с простыми свойствами
struct kv_codec {
  template <typename T>
  static void serialize(const T& obj, std::string_view, svarg::content_type_params&, http_body_bytes& out) {
    std::string s;
    FOR_EACH_FIELD_INDEX_OF(T) {
      if (is_field_present<I>(obj))
        s += std::format("{}{}={}", s.empty() ? "" : ";", get_field_name<I, T>(),
                         serialize_nonbody_into(get_field<I>(obj)));
    };
    append(out, s);
  }
  template <typename T>
  static svarg::error_code parse(std::string_view, std::string_view data, T& out) {
    while (!data.empty()) {
      std::string_view pair = data.substr(0, data.find(';'));
      data.remove_prefix(std::min(data.size(), pair.size() + 1));
      size_t eq = pair.find('=');
      std::optional i = get_field_index<T>(pair.substr(0, eq));
      if (!i || eq == pair.npos)
        return svarg::parse_error("bad kv pair `{}`", pair);
      svarg::error_code ec = visit_field_index<fields_count<T>()>(
          [&]<size_t I>() { return parse_nonbody_from(pair.substr(eq + 1), get_field<I>(out)); }, *i);
      if (ec)
        return ec;
    }
    return {};
  }
};

// строка в верхнем регистре на проводе
struct upper_codec {
  static void serialize(const std::string& v, std::string_view, svarg::content_type_params&,
                        http_body_bytes& out) {
    std::string s = v;
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::toupper(c); });
    append(out, s);
  }
  static svarg::error_code parse(std::string_view, std::string_view data, std::string& out) {
    out = data;
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
    return {};
  }
};

// тип формата date: svarg --add-format string+date=test_codecs::date
struct date {
  int y = 0, m = 0, d = 0;

  bool operator==(const date&) const = default;
};

}  // namespace test_codecs

// текст (параметры) и JSON для типа формата
template <>
struct svarg::cxx_support::text_value<test_codecs::date> {
  static std::string serialize(const test_codecs::date& v) {
    return std::format("{:04}-{:02}-{:02}", v.y, v.m, v.d);
  }
  static svarg::error_code parse(std::string_view s, test_codecs::date& v) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' || !svarg::try_from_chars(s.substr(0, 4), v.y) ||
        !svarg::try_from_chars(s.substr(5, 2), v.m) || !svarg::try_from_chars(s.substr(8, 2), v.d))
      return svarg::parse_error("bad date `{}`", s);
    return {};
  }
};

template <>
struct svarg::to_json_value<test_codecs::date> {
  boost::json::value operator()(const test_codecs::date& v) const {
    return boost::json::string(svarg::cxx_support::text_value<test_codecs::date>::serialize(v));
  }
};

template <>
struct svarg::from_json_value<test_codecs::date> {
  svarg::error_code operator()(const boost::json::value& j, test_codecs::date& v) const {
    if (!j.is_string())
      return svarg::parse_error("date must be a string");
    return svarg::cxx_support::text_value<test_codecs::date>::parse(j.get_string(), v);
  }
};

template <>
struct svarg::cxx_support::media_codec<"application/x-kv"> : test_codecs::kv_codec {};

// по суффиксу: любой медиа тип ...+kv кодируется как application/x-kv
template <>
struct svarg::cxx_support::media_type_base<"+kv"> {
  static constexpr std::string_view value = "application/x-kv";
};

// по точному медиа типу
template <>
struct svarg::cxx_support::media_codec<"application/x-upper"> : test_codecs::upper_codec {};

// JSON без суффикса +json
template <>
struct svarg::cxx_support::media_type_base<"application/vnd.test.object"> {
  static constexpr std::string_view value = "application/json";
};

// хуки для всех операций спеки с хедером authorization
template <svarg::cxx_support::an_operation Op>
  requires(Op::spec == "api" && requires(Op op) { op.headers.authorization; })
struct svarg::cxx_support::operation_hooks<Op> {
  static void before_send(const Op& op, hidi::http_request& req) {
    if (!op.headers.authorization && test_codecs::current_token)
      req.headers.emplace_back("authorization", "Bearer " + *test_codecs::current_token);
  }
  static dd::task<hidi::http_response> do_handle_request(Op& op, hidi::http_request& req, auto& server,
                                                         hidi::request_context& ctx) {
    if (op.headers.authorization == "Bearer bad")
      return ready_response(hidi::http_response{.status = 403});
    if (op.headers.authorization == "Bearer exchange")
      return exchange_then_handle(std::move(op), std::move(req), server, std::move(ctx));
    return {};
  }
  // асинхронно: аргументы перемещены в корутину, затем обработка по умолчанию
  static dd::task<hidi::http_response> exchange_then_handle(Op op, hidi::http_request req, auto& server,
                                                            hidi::request_context ctx) {
    (void)co_await ready_response({});  // как будто сходили в базу
    op.headers.authorization = "Bearer good";
    co_return co_await server.handle(std::move(op), std::move(req), std::move(ctx));
  }
};
