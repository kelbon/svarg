#pragma once

#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/x_www_form_urlencoded.hpp"
#include "svarg/backend/cxx/support/names.hpp"

#include "svarg/utils/path_tools.hpp"

#include <http2/http_base.hpp>

namespace svarg::cxx_support {

template <an_operation Op>
consteval http2::http_method_e get_operation_method() {
  http2::http_method_e method;
  if (!efromstr(Op::method, method))
    throw "invalid method";
  return method;
}

// из выражения Path::path_expr типа /abc/{x} делает /abc/{} для подставления в качестве форматной строки
template <typename Path>
consteval auto make_format_from_path_expr() {
  // длина за вычетом всех имён внутри шаблонных сегментов
  constexpr size_t len = [] {
    size_t r = 0;
    visit_path_segments(Path::path_expr, [&](std::string_view seg) {
      if (seg.starts_with('{')) {
        if (!seg.ends_with('}'))
          throw "bad segment";
        r += seg.size() - 2;
      }
    });
    return Path::path_expr.size() - r;
  };
  std::array<char, len> fmt;
  char* out = fmt.data();
  visit_path_segments(Path::path_expr, [&](std::string_view seg) {
    *out = '/';
    ++out;

    if (seg.starts_with('{')) {
      *out = '{';
      ++out;
      *out = '}';
      ++out;
    } else {
      out = std::copy_n(seg.data(), seg.size(), out);
    }
  });
  assert(out == fmt.data() + fmt.size());
  return fmt;
}

// TODO rvalue нужно чтобы строки уметь забирать из операции?

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
    // в данном случае json означает "по дефолту просто string -> string, int -> string"
    result += serialize_field_into<content_type_e::application_json>(p.path, field_index);
  }
  template <size_t I>
  void param(std::index_sequence<I> is, std::string_view separator) {
    param(is);
    substr(separator);
  }
};

template <an_operation Op>
std::string make_path(const Op& op) {
  serialize_path_visitor<Op> vtor(op);
  Op::visit_path(vtor);
  return std::move(vtor.result);
}

template <an_operation Op>
http2::http_request make_request(const Op& op) {
  http2::http_request req;
  // req.scheme, req.authority здесь игнорируется, заполняется уже клиентом при отправке

  if constexpr (has_body<Op>) {
    if (!is_body_present(op)) {
      if constexpr (is_body_required<Op>()) {
        throw serialize_error("required body for operation `{}` is not present", type_name<Op>());
      }
    } else {
      // is_body_present(op) == true
      req.headers.emplace_back(std::string(content_type_hdr), std::string(get_content_type(op)));
    }
  }
  req.path = make_path<Op::path_t>(op);
  if constexpr (has_query_params<Op>) {
    application_x_www_form_urlencoded query;
    FOR_EACH_FIELD_INDEX_OF(query_t<Op>) {
      if (is_field_present<I>(op.query))
        query.arg<content_type_e::application_json>(get_field_name<I, query_t<Op>>,
                                                    get_presented_field<I>(op.query));
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

  if constexpr (has_header_params<Op>) {
    FOR_EACH_FIELD_INDEX_OF(headers_t<Op>) {
      // cookies хедер должен быть только из cookie параметров
      static_assert(!is_equal_ci(get_field_name<I, headers_t<Op>>(), cookie_hdr));
      if (is_field_present<I>(op.headers)) {
        req.headers.emplace_back(get_field_name<I, headers_t<Op>>(),
                                 serialize_field_into<content_type_e::application_json, I>(op.headers));
      }
    };
  }

  if constexpr (has_cookie_params<Op>) {
    std::string cookie_header;
    FOR_EACH_FIELD_INDEX_OF(cookies_t<Op>) {
      if (is_field_present<I>(op.cookies)) {
        cookie_header += get_field_name<I, cookies_t<Op>>();
        cookie_header += '=';
        // app/json тут также выполняет роль просто "дефолтно закодируй если нет encoding"
        cookie_header += serialize_field_into<content_type_e::application_json, I>(op.cookies);
        if constexpr (I != (fields_count<cookies_t<Op>>() - 1)) {
          // https://datatracker.ietf.org/doc/html/rfc6265.html?utm_source=chatgpt.com#section-4.2.1
          // формально по грамматике пробел действительно обязателен
          cookie_header += "; ";
        }
      }
      if (!cookie_header.empty())
        req.headers.emplace_back(std::string(cookie_hdr), std::move(cookie_header));
    };
  }

  if constexpr (has_body<Op>) {
    visit_body(op, [&]<typename B>(const B& x) {
      req.body = serialize_into<get_content_type<body_t<Op>>()>(op.body);
    });
  }

  return req;
}

template <an_operation Op>
Op from_request(http2::http_request) {
  // TODO
}

}  // namespace svarg::cxx_support
