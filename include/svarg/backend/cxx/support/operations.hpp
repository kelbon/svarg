#pragma once

#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/nonbody.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/x_www_form_urlencoded.hpp"
#include "svarg/backend/cxx/support/names.hpp"

#include "svarg/utils/path_tools.hpp"

#include "svarg/backend/cxx/support/any_client.hpp"

#include <hidi/http_base.hpp>

namespace svarg::cxx_support {

// TODO consteval (efromstr in HTTP/2)
template <an_operation Op>
constexpr http_method_e get_operation_method() {
  http_method_e method;
  if (!efromstr(Op::method, method))
    throw "invalid method";
  return method;
}

// обрабатывает корректно encoding
// `Into` - енкодинг по умолчанию в который идёт запись
// `FieldIndex` - индекс поля которое нужно сериализовать
// `T` - тип внутри которого поле сериализуется
template <content_type_e Into, size_t FieldIndex, typename T>
std::string serialize_field_into(const T& f, std::index_sequence<FieldIndex> = {}) {
  constexpr content_type_e encoding = [] {
    if constexpr (has_encodings<T>) {
      constexpr std::string_view enc = T::encoding_for(get_field_name<FieldIndex, T>());
      if (enc.empty())
        return Into;
      content_type_e ct;
      if (efromstr(enc, ct))
        return ct;
      throw serialize_error("unsupported content type `{}`", enc);
    } else {
      return Into;
    }
  };
  return serialize_into<encoding>(get_field<FieldIndex>(f));
}

template <size_t FieldIndex, typename T>
std::string serialize_nonbody_field(const T& f, std::index_sequence<FieldIndex> = {}) {
  if constexpr (has_encodings<T>) {
    constexpr std::string_view enc = T::encoding_for(get_field_name<FieldIndex, T>());
    if (enc.empty())
      return serialize_nonbody_into(get_field<FieldIndex>(f));
    constexpr content_type_e ct = [&] {
      content_type_e e;
      if (efromstr(enc, e))
        return e;
      throw serialize_error("unsupported content type `{}`", enc);
    };
    return serialize_into<ct>(get_field<FieldIndex>(f));
  } else {
    return serialize_nonbody_into(get_field<FieldIndex>(f));
  }
}

template <an_operation Op>
struct serialize_path_visitor {
  const Op& p;
  std::string result;

  void substr(std::string_view s) {
    result += s;
  }
  template <size_t I>
  void param(std::index_sequence<I> field_index) {
    static_assert(has_path_params<Op>);
    result += serialize_nonbody_field(p.path, field_index);
  }
  template <size_t I>
  void param(std::index_sequence<I> field_index, std::string_view separator) {
    static_assert(has_path_params<Op>);
    auto part = serialize_nonbody_field(p.path, field_index);
    // separator внутри самой части пути приведёт к неправильному парсу пути на другой стороне
    // например /v1/{a}-{b}. Если {a} содержит '-', то /v1/part-a-cd распарсится не как a=part-a, b=cd,
    // а как a=part, b=a-cd
    assert(!part.contains(separator));
    result += std::move(part);
    substr(separator);
  }
};

template <an_operation Op>
auto make_path(const Op& op) {
  if constexpr (!has_path<Op>) {
    return std::string_view{};
  } else {
    if constexpr (!has_path_params<Op>) {
      return std::string_view(Op::path_expr);
    } else {
      serialize_path_visitor<Op> vtor(op);
      Op::visit_path(vtor);
      return std::move(vtor.result);
    }
  }
}
// TODO тесты на каждую часть
template <an_operation Op>
struct parse_path_visitor {
  Op& op;
  std::string_view path;
  error_code ec;

  // visit_path не проверяет is_done, поэтому после ошибки все вызовы пропускаются
  void substr(std::string_view s) {
    if (ec)
      return;
    if (!path.starts_with(s)) [[unlikely]] {
      ec = parse_error("path `{}` does not match `{}`", path, s);
      return;
    }
    path.remove_prefix(s.size());
  }
  template <size_t I>
  void param(std::index_sequence<I> field_index) {
    static_assert(has_path_params<Op>);
    if (ec)
      return;
    ec = parse_nonbody_from(path, get_field<I>(op.path));
    path = {};
  }
  template <size_t I>
  void param(std::index_sequence<I> field_index, std::string_view separator) {
    static_assert(has_path_params<Op>);
    if (ec)
      return;
    auto pos = path.find(separator);
    if (pos == path.npos) {
      ec = parse_error("path `{}` does not contain separator `{}`", path, separator);
      return;
    }
    std::string_view arg = path.substr(0, pos);
    path.remove_prefix(pos + separator.size());
    ec = parse_nonbody_from(arg, get_field<I>(op.path));
  }

  bool is_done() const {
    return ec.failed();
  }
  void error(std::string_view msg) {
    ec = parse_error("{}", msg);
  }
};

// ожидает на вход path без query и фрагментов
template <an_operation Op>
[[nodiscard]] error_code parse_path_params(std::string_view path, Op& op) {
  if constexpr (!has_path<Op>)
    return {};
  else {
    if constexpr (!has_path_params<Op>) {
      assert(path == Op::path_expr);  // сервер не должен вызывать такой парсинг
      return {};
    } else {
      parse_path_visitor<Op> vtor{.op = op, .path = path};
      Op::visit_path(vtor);
      if (!vtor.ec && !vtor.path.empty())
        return parse_error("unexpected path suffix `{}`", vtor.path);
      return std::move(vtor.ec);
    }
  }
}

template <typename T>
void serialize_headers_of(T&& v, http_headers_t& out) {
  if constexpr (has_header_params<std::decay_t<T>>) {
    FOR_EACH_FIELD_INDEX_OF(headers_t<T>) {
      if (is_field_present<I>(v.headers))
        out.emplace_back(std::string(get_field_name<I, headers_t<T>>()),
                         serialize_nonbody_field<I>(v.headers));
    };
  }
}

namespace noexport {

// тело типа `B` внутри операции/ответа `x`, создаётся если его нет
template <typename B, typename T>
[[nodiscard]] B& emplace_body(T& x) {
  if constexpr (!has_single_body<T>())
    return x.body.template emplace<B>();
  else if constexpr (is_body_required<T>())
    return x.body;
  else
    return x.body.emplace();
}

// вызывает `out(content_type_header_value, bytes)`, если тело присутствует
template <typename T>
void serialize_body_of(const T& x, const content_type_params& ctparams, auto&& out) {
  if (!is_body_present(x))
    return;
  visit_body(x, [&]<typename B>(const B& b) {
    out(media_type_to_hdr_value(get_content_type_enum<B>(), ctparams), serialize_body(b, ctparams));
  });
}

// пустые content type и данные означают отсутствие тела
template <typename T>
[[nodiscard]] error_code parse_body_of(std::string_view content_type, std::string_view data, T& x) {
  if (content_type.empty() && data.empty()) {
    if constexpr (is_body_required<T>())
      return parse_error("body is required for `{}`", type_name<T>());
    else
      return {};
  }
  return select_body_type<T>(content_type, [&]<typename B>() -> error_code {
    if constexpr (std::is_void_v<B>)
      return parse_error("content type `{}` not matched any body of `{}`", content_type, type_name<T>());
    else
      return parse_from<get_content_type_enum<B>()>(data, emplace_body<B>(x));
  });
}

}  // namespace noexport

// не заполняет req.path, если у операции нет пути (вебхук...)
// складывает content_type не в headers, а в req.body.content_type
// не выставляет req.scheme и req.authority, это должно заполняться уже клиентом при отправке
// `base_path` - путь из url сервера (например "/v1"), добавляется перед путём операции
template <an_operation Op>
http_request make_request(const Op& op, const content_type_params& ctparams = {},
                          std::string_view base_path = {}) {
  http_request req;

  req.path = base_path;
  req.path += make_path<Op>(op);
  if constexpr (has_query_params<Op>) {
    application_x_www_form_urlencoded query;
    FOR_EACH_FIELD_INDEX_OF(query_t<Op>) {
      if (is_field_present<I>(op.query))
        query.nonbody_arg(get_field_name<I, query_t<Op>>(), serialize_nonbody_field<I>(op.query));
    };
    // TODO вынести в отдельную функцию по работе с квери
    std::string q = query.take();
    if (!q.empty()) {
      req.path.push_back('?');
      req.path += q;
      // нужно только если дальше fragment
      // req.path.push_back('#');
    }
  }

  req.method = get_operation_method<Op>();

  serialize_headers_of(op, req.headers);

  if constexpr (has_cookie_params<Op>) {
    std::string cookie_header;
    FOR_EACH_FIELD_INDEX_OF(cookies_t<Op>) {
      if (is_field_present<I>(op.cookies)) {
        if (!cookie_header.empty()) {
          // https://datatracker.ietf.org/doc/html/rfc6265.html#section-4.2.1
          // формально по грамматике пробел действительно обязателен
          cookie_header += "; ";
        }
        cookie_header += get_field_name<I, cookies_t<Op>>();
        cookie_header += '=';
        cookie_header += serialize_nonbody_field<I>(op.cookies);
      }
    };
    if (!cookie_header.empty())
      req.headers.emplace_back(std::string(cookie_hdr), std::move(cookie_header));
  }

  if constexpr (has_body<Op>) {
    noexport::serialize_body_of(op, ctparams, [&](std::string ct, http_body_bytes data) {
      req.body.content_type = std::move(ct);
      req.body.data = std::move(data);
    });
  }

  return req;
}

namespace noexport {

parse_error unparsed_error(const auto& unparsed, std::string_view what) {
  assert(unparsed.any());
  return parse_error("not all required {} args are present, unparsed fields: {}", what, unparsed.to_string());
}

}  // namespace noexport

// content-type берётся не из headers, а из req.body.content_type
// `base_path` - путь из url сервера (например "/v1"), с которого начинается req.path
template <an_operation Op>
[[nodiscard]] error_code from_request(const http_request& req, Op& op, std::string_view base_path = {}) {
  assert(req.method == get_operation_method<Op>());

  error_code ec;

  [[maybe_unused]] std::string_view path = path_and_query(req.path).first;
  if (!path.starts_with(base_path))
    return parse_error("path `{}` does not start with base path `{}`", path, base_path);
  path.remove_prefix(base_path.size());

  if constexpr (has_path_params<Op>) {
    ec = parse_path_params(path, op);
    if (ec)
      return ec;
  }

  if constexpr (has_query_params<Op>) {
    std::bitset unparsed = cxx_support::get_required_bits_mask<query_t<Op>>();
    auto on_query_arg = [&](std::string_view name, std::string_view value) -> error_code {
      std::optional index = get_field_index<query_t<Op>>(name);
      if (!index)
        return {};  // игнорируем дополнительные аргументы
      unparsed.set(*index, false);
      return metel::visit_index<fields_count<query_t<Op>>() - 1>(
          [&]<size_t I>() { return parse_nonbody_from(value, get_field<I>(op.query)); }, *index);
    };
    ec = visit_query</*WwwUrlEncoded=*/false>(get_query(req.path), on_query_arg);
    if (ec)
      return ec;
    if (unparsed.any())
      return noexport::unparsed_error(unparsed, "query");
  }

  auto unparsed_cookies = [] {
    if constexpr (has_cookie_params<Op>) {
      return get_required_bits_mask<cookies_t<Op>>();
    } else {
      return std::monostate{};
    }
  }();
  auto unparsed_headers = [] {
    if constexpr (has_header_params<Op>) {
      return get_required_bits_mask<headers_t<Op>>();
    } else {
      return std::monostate{};
    }
  }();

  for (auto&& [name, value] : req.headers) {
    if (!is_lowered(name)) [[unlikely]] {
      // привязано к HTTP/2
      ec = parse_error("header name `{}` is not lowered, HTTP/2 requires lower case", name);
      return ec;
    }
    if constexpr (has_cookie_params<Op>) {
      if (name == cookie_hdr) {
        auto on_cookie_arg = [&](std::string_view name, std::string_view value) {
          std::optional index = get_field_index<cookies_t<Op>>(name);
          if (!index)
            return error_code{};  // игнорируем дополнительные аргументы
          unparsed_cookies.set(*index, false);
          return metel::visit_index<fields_count<cookies_t<Op>>() - 1>(
              [&]<size_t I>() { return parse_nonbody_from(value, get_field<I>(op.cookies)); }, *index);
        };
        ec = visit_cookies(value, on_cookie_arg);
        if (ec)
          return ec;
        continue;
      }
    }
    if constexpr (has_header_params<Op>) {
      std::optional index = get_field_index<headers_t<Op>>(name);
      if (index) {
        unparsed_headers.set(*index, false);
        ec = metel::visit_index<fields_count<headers_t<Op>>() - 1>(
            [&]<size_t I>() { return parse_nonbody_from(value, get_field<I>(op.headers)); }, *index);
        if (ec)
          return ec;
      }
      continue;
    }
  }
  if constexpr (has_cookie_params<Op>) {
    if (unparsed_cookies.any())
      return noexport::unparsed_error(unparsed_cookies, "cookies");
  }
  if constexpr (has_header_params<Op>) {
    if (unparsed_headers.any())
      return noexport::unparsed_error(unparsed_headers, "header");
  }

  if constexpr (has_body<Op>) {
    ec = noexport::parse_body_of(req.body.content_type, req.body.strview(), op);
    if (ec)
      return ec;
  }

  return ec;
}

template <a_response R>
[[nodiscard]] http_response make_response(const R& rsp, const content_type_params& ctparams = {}) {
  http_response rawrsp;
  assert(rsp.status != 0);  // unset
  rawrsp.status = rsp.status;
  serialize_headers_of(rsp, rawrsp.headers);

  if constexpr (has_body<R>) {
    noexport::serialize_body_of(rsp, ctparams, [&](std::string ct, http_body_bytes data) {
      rawrsp.headers.emplace_back(std::string(content_type_hdr), std::move(ct));
      rawrsp.body = std::move(data);
    });
  }
  return rawrsp;
}

// true, если `R` один из ответов операции `Op`
template <an_operation Op, typename R>
constexpr bool is_response_of = [] {
  using result = typename Op::result_t;
  if constexpr (std::is_same_v<result, R>)
    return true;
  else if constexpr (requires { result::data; })
    return []<typename... Ts>(std::type_identity<std::variant<Ts...>>) {
      return (std::is_same_v<Ts, R> || ...);
    }(std::type_identity<decltype(result::data)>{});
  else
    return false;
}();

// `op` только для проверки на этапе компиляции, что ответ принадлежит операции
template <an_operation Op, a_response R>
[[nodiscard]] http_response make_response(const Op&, const R& rsp, const content_type_params& ctparams = {}) {
  static_assert(is_response_of<Op, R>, "response is not declared for this operation");
  return make_response(rsp, ctparams);
}

template <a_response R>
error_code from_response(const http_response& rawrsp, R& rsp) {
  static_assert(metel::is_decayed_v<R>);

  error_code ec;

  if constexpr (requires { rsp.status = 0; }) {  // не static constexpr status
    if constexpr (requires { R::status_range; }) {
      if (!match_status_range<R::status_range>(rawrsp.status)) {
        return parse_error("status `{}` do not match status range `{}`", rawrsp.status,
                           e2str(R::status_range));
      }
    }
    rsp.status = rawrsp.status;
  }

  [[maybe_unused]] std::string_view content_type;
  auto unparsed = [] {
    if constexpr (has_header_params<R>)
      return get_required_bits_mask<headers_t<R>>();
    else
      return std::monostate{};
  }();

  for (auto&& [name, value] : rawrsp.headers) {
    if (!is_lowered(name)) {
      return validate_error("HTTP/2 requires all header names to be lowercase, name `{}` is not lowercase",
                            name);
    }
    if constexpr (has_body<R>) {
      // первый хедер content-type выигрывает
      if (content_type.empty() && name == content_type_hdr) {
        content_type = value;
        continue;
      }
    }
    if constexpr (has_header_params<R>) {
      std::optional index = get_field_index<headers_t<R>>(name);
      if (index) {
        unparsed.set(*index, false);
        ec = metel::visit_index<fields_count<headers_t<R>>() - 1>(
            [&]<size_t I>() { return parse_nonbody_from(value, get_field<I>(rsp.headers)); }, *index);
        if (ec)
          return ec;
      }
    }
  }
  if constexpr (has_header_params<R>) {
    if (unparsed.any())
      return noexport::unparsed_error(unparsed, "header");
  }
  if constexpr (has_body<R>) {
    ec = noexport::parse_body_of(content_type, rawrsp.body_strview(), rsp);
    if (ec)
      return ec;
  }
  return {};
}

template <an_operation Op>
using operation_result = typename Op::result_t;

namespace noexport {

// result_t - либо сам ответ, либо структура с вариантом ответов
template <typename T, typename Result>
[[nodiscard]] T& emplace_result(Result& r) {
  if constexpr (std::is_same_v<Result, T>)
    return r;
  else
    return r.data.template emplace<T>();
}

}  // namespace noexport

// отдельно принимает операцию и отдельно make_request чтобы легче контролировать параметры content_type
template <an_operation Op>
[[nodiscard]] dd::task<operation_result<Op>> send_request(any_client& client, http_request req,
                                                          deadline_t deadline) {
  static_assert(metel::is_decayed_v<Op>);
  http_response rsp = co_await client.send_request(std::move(req), deadline);
  // TODO sax parsing
  operation_result<Op> result;
  auto do_parse = [&]<typename T>() {
    if constexpr (std::is_void_v<T>) {
      throw parse_error("status `{}` do not match any response for operation `{}`", rsp.status,
                        type_name<Op>());
    } else {
      from_response(rsp, noexport::emplace_result<T>(result)).rethrow_if_failed();
    }
  };
  Op::visit_response_status(rsp.status, do_parse);
  co_return result;
}

}  // namespace svarg::cxx_support
