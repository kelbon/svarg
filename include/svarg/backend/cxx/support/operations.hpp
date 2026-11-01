#pragma once

#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/nonbody.hpp"
#include "svarg/backend/cxx/support/body.hpp"
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

template <an_operation Op>
struct serialize_path_visitor {
  const Op& p;
  std::string result;

  void substr(std::string_view s) {
    result += s;
  }
  // значения кодируются (percent_encode_path_value), литералы шаблона уже валидны
  template <size_t I>
  void param(std::index_sequence<I> field_index) {
    static_assert(has_path_params<Op>);
    percent_encode_path_value(serialize_nonbody_field(p.path, field_index), "", std::back_inserter(result));
  }
  template <size_t I>
  void param(std::index_sequence<I> field_index, std::string_view separator) {
    static_assert(has_path_params<Op>);
    percent_encode_path_value(serialize_nonbody_field(p.path, field_index), separator,
                              std::back_inserter(result));
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
  std::string decode_buf;

  template <size_t I>
  void parse_param(std::index_sequence<I>, std::string_view raw) {
    ec = parse_nonbody_field<I>(percent_decode_path_value(raw, decode_buf), op.path);
  }

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
    parse_param(field_index, path);
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
    parse_param(field_index, arg);
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

// вызывает vtor.template operator()<формат>(медиа тип, значение тела), если тело присутствует
template <typename T, typename Vtor>
void visit_body(const T& x, Vtor&& vtor) {
  if (!is_body_present(x)) {
    if constexpr (is_body_required<T>())
      throw serialize_error("body is required for `{}`, but not present", type_name<T>());
    return;
  }
  if constexpr (has_single_body<T>()) {
    constexpr body_format_e F = body_format_of(T::body_content_type);
    if constexpr (is_body_required<T>())
      vtor.template operator()<F>(T::body_content_type, x.body);
    else
      vtor.template operator()<F>(T::body_content_type, *x.body);
  } else {
    std::visit(metel::matcher{
                   [](std::monostate) { metel::unreachable(); },
                   [&]<typename B>(const B& b) {
                     vtor.template operator()<body_format_of(get_content_type<B>())>(get_content_type<B>(),
                                                                                     get_special_field(b));
                   },
               },
               x.body);
  }
}

// вызывает `out(content_type_header_value, bytes)`, если тело присутствует
template <typename T>
void serialize_body_of(const T& x, const content_type_params& ctparams, auto&& out) {
  visit_body(x, [&]<body_format_e F>(std::string_view media_type, const auto& value) {
    if constexpr (F == body_format_e::multipart) {
      content_type_params params = ctparams;
      http_body_bytes bytes = serialize_body<F>(value, media_type, params);
      out(media_type_to_hdr_value(media_type, params), std::move(bytes));
    } else {
      content_type_params unused;
      http_body_bytes bytes = serialize_body<F>(value, media_type, unused);
      out(media_type_to_hdr_value(media_type, ctparams), std::move(bytes));
    }
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
  auto not_matched = [&] {
    return parse_error("content type `{}` not matched any body of `{}`", content_type, type_name<T>());
  };
  if constexpr (has_single_body<T>()) {
    // учитывает регистр и параметры (application/json; charset=utf-8)
    if (!match_content_type(T::body_content_type, content_type))
      return not_matched();
    constexpr body_format_e F = body_format_of(T::body_content_type);
    if constexpr (is_body_required<T>())
      return parse_body<F>(content_type, data, x.body);
    else
      return parse_body<F>(content_type, data, x.body.emplace());
  } else {
    return T::select_body_type(content_type, [&]<typename B>() -> error_code {
      if constexpr (std::is_void_v<B>)
        return not_matched();
      else
        return parse_body<body_format_of(get_content_type<B>())>(
            content_type, data, get_special_field(x.body.template emplace<B>()));
    });
  }
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
      return visit_field_index<fields_count<query_t<Op>>()>(
          [&]<size_t I>() { return parse_nonbody_field<I>(value, op.query); }, *index);
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
          return visit_field_index<fields_count<cookies_t<Op>>()>(
              [&]<size_t I>() { return parse_nonbody_field<I>(value, op.cookies); }, *index);
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
        ec = visit_field_index<fields_count<headers_t<Op>>()>(
            [&]<size_t I>() { return parse_nonbody_field<I>(value, op.headers); }, *index);
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
        ec = visit_field_index<fields_count<headers_t<R>>()>(
            [&]<size_t I>() { return parse_nonbody_field<I>(value, rsp.headers); }, *index);
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

// бросается клиентом, если статус ответа не описан для операции (и у неё нет default ответа)
struct unexpected_response : svarg_exception {
  http_response response;

  unexpected_response(http_response rsp, std::string_view operation)
      : svarg_exception("status `{}` is not described for operation `{}`", rsp.status, operation),
        response(std::move(rsp)) {
  }
};

// описанные в спецификации ответы операции: сам ответ, либо структура с вариантом ответов
template <an_operation Op>
using operation_result = typename Op::result_t;

namespace noexport {

template <typename Result, typename T>
[[nodiscard]] Result make_result(T&& v) {
  if constexpr (std::is_same_v<Result, std::decay_t<T>>)
    return std::forward<T>(v);
  else
    return Result{.data = std::forward<T>(v)};
}

}  // namespace noexport

// отдельно принимает операцию и отдельно make_request чтобы легче контролировать параметры content_type
// бросает исключение при ошибке транспорта, неразобранном ответе и unexpected_response
template <an_operation Op>
[[nodiscard]] dd::task<operation_result<Op>> send_request(any_client& client, http_request req,
                                                          deadline_t deadline) {
  static_assert(metel::is_decayed_v<Op>);
  http_response rsp = co_await client.send_request(std::move(req), deadline);
  // TODO sax parsing
  co_return Op::visit_response_status(rsp.status, [&]<typename T>() -> operation_result<Op> {
    if constexpr (std::is_void_v<T>) {
      throw unexpected_response(std::move(rsp), type_name<Op>());
    } else {
      T v;
      from_response(rsp, v).rethrow_if_failed();
      return noexport::make_result<operation_result<Op>>(std::move(v));
    }
  });
}

}  // namespace svarg::cxx_support
