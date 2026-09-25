#pragma once

#include <format>
#include <optional>
#include <string_view>
#include <concepts>

#include <strswitch/strswitch.hpp>

#include <boost/pfr.hpp>

#include <metel/meta.hpp>

#include <kelcoro/generator.hpp>

#include "svarg/utils/forward_like.hpp"
#include "svarg/backend/cxx/support/types.hpp"
#include "svarg/utils/parse_frontend_or_ir_type.hpp"
#include "svarg/utils/from_chars.hpp"
#include "svarg/utils/path_tools.hpp"

#include "svarg/backend/cxx/support/http_base.hpp"

// эти 2 макроса только для генерируемых типов
#define APPLY_FIELD_INDEXES(T) APPLY_INDEXES(::svarg::cxx_support::fields_count<T>())
#define FOR_EACH_FIELD_INDEX_OF(T) FOR_EACH_INDEX(::svarg::cxx_support::fields_count<T>())

// описание генерируемых по умолчанию структур - операций и схем

namespace svarg::noexport {

template <typename T>
struct deoptional_type : std::type_identity<T> {};

template <typename T>
struct deoptional_type<std::optional<T>> : std::type_identity<T> {};

template <typename T>
struct deoptional_type<metel::box<T>> : std::type_identity<T> {};

template <typename T>
using deoptional_type_t = typename deoptional_type<std::decay_t<T>>::type;

}  // namespace svarg::noexport

namespace svarg::cxx_support {

enum struct parse_tag_e {
  // просто комбинаторы, недостаточно тривиальные чтобы сделать их алиасами

  // имя "спецполя" = <special_field_name>
  // у каждого может быть using required_schemas = typelist<...>;

  fundamental,  // какой-то фундаментал с ограничениями
  array,        // одно спецполе array
  anyof,        // одно спецполе anyof + возможен discriminator_field + discriminate(string_view, auto&& vtor)
  oneof,        // одно спецполе anyof + возможен discriminator_field + discriminate(string_view, auto&& vtor)
  allof,        // одно спецполе - allof с именем <special_field_name>
  not_a,        // одно спецполе

  // не комбинаторы
  object,  // сначала требуемые поля, затем опциональные + is_required(string_view) +
           // additional_properties_type / additional_properties
  enum_,
  string_enum,

  body,       // string_view content_type + <special_field_name> единственное поле с данными +
              // encoding_for(string_view)
  operation,  // вообще парсится из запроса и сериализуется в запрос
  response,   // status/statusRange/нет статуса для дефолтного + хедера параметрами + боди
};

constexpr std::string_view e2str(parse_tag_e e) noexcept {
  switch (e) {
    case parse_tag_e::fundamental:
      return "fundamental";
    case parse_tag_e::array:
      return "array";
    case parse_tag_e::anyof:
      return "anyof";
    case parse_tag_e::oneof:
      return "oneof";
    case parse_tag_e::allof:
      return "allof";
    case parse_tag_e::not_a:
      return "not_a";
    case parse_tag_e::object:
      return "object";
    case parse_tag_e::enum_:
      return "enum_";
    case parse_tag_e::string_enum:
      return "string_enum";
    case parse_tag_e::body:
      return "body";
    case parse_tag_e::operation:
      return "operation";
    case parse_tag_e::response:
      return "response";
  }
  metel::unreachable();
}

[[nodiscard]] constexpr bool efromstr(std::string_view s, parse_tag_e& out) {
  std::optional t = ss::string_switch<parse_tag_e>(s)
                        .case_("fundamental", parse_tag_e::fundamental)
                        .case_("array", parse_tag_e::array)
                        .case_("anyof", parse_tag_e::anyof)
                        .case_("oneof", parse_tag_e::oneof)
                        .case_("allof", parse_tag_e::allof)
                        .case_("not_a", parse_tag_e::not_a)
                        .case_("object", parse_tag_e::object)
                        .case_("enum_", parse_tag_e::enum_)
                        .case_("string_enum", parse_tag_e::string_enum)
                        .case_("body", parse_tag_e::body)
                        .case_("operation", parse_tag_e::operation)
                        .case_("response", parse_tag_e::response)
                        .or_nullopt();
  if (!t)
    return false;
  out = *t;
  return true;
}

// TODO генерировать геттеры сеттеры для объектов и вариантов (get if/is/visit)
// TODO генерировать серверы и клиенты - у сервера адрес по выражению, функция которая определяет какой запрос
// пришёл по path и сами обработчики (видимо виртуальные функции для удобства пользователя при
// переопределении). В одной спеке я так понимаю может быть много серверов, но как будто у каждой спеки на
// деле один сервер со всеми её операциями
// Клиент - а вот тут вопрос, видимо тоже один на спеку? типа все запросы отправлять
// но у него не виртуальные, просто методы которые принимают операцию, разница от обычного шаблона просто в
// именованности. Теоретически можно ещё аргументы спрятать в клиенте общие, типа авторизации, но хз
// В общем надо парсинг, сериализацию, валидацию запросв при отправке, саму отправку запроса/получение ответа,
// и выбор правильного обработчика в сервере. Необработанное идёт в виртуальную функцию unhandled_request
// который по умолчанию бросает 500

template <typename T>
constexpr bool has_required_schemas = requires { typename T::required_schemas; };

template <typename T>
concept has_parse_tag = requires {
  { std::decay_t<T>::parse_tag } -> std::same_as<const parse_tag_e&>;
};

template <typename T>
constexpr bool tagged_as(parse_tag_e t) noexcept {
  if constexpr (has_parse_tag<T>)
    return t == std::decay_t<T>::parse_tag;
  else
    return false;
}

namespace noexport {

// пример функции для концепта. (лямбда в require сломает компилятор)
struct noop_fn {
  template <typename>
  void operator()() {
  }
};

}  // namespace noexport

// oneof/anyof
template <typename T>
constexpr bool has_discriminator = requires(std::string_view discriminator_value) {
  { T::discriminator_field } -> std::convertible_to<std::string_view>;
  { T::discriminate(discriminator_value, noexport::noop_fn{}) };
};

template <typename T>
constexpr inline parse_tag_e parse_tag_of = std::decay_t<T>::parse_tag;

template <typename T>
concept an_fundamental = tagged_as<T>(parse_tag_e::fundamental);

template <typename T>
concept an_array = tagged_as<T>(parse_tag_e::array);

template <typename T>
concept an_anyof = tagged_as<T>(parse_tag_e::anyof);

template <typename T>
concept a_oneof = tagged_as<T>(parse_tag_e::oneof);

template <typename T>
concept an_allof = tagged_as<T>(parse_tag_e::allof);

template <typename T>
concept a_not_a = tagged_as<T>(parse_tag_e::not_a);

template <typename T>
concept an_object = tagged_as<T>(parse_tag_e::object);

template <typename T>
concept an_enum = tagged_as<T>(parse_tag_e::enum_);

template <typename T>
concept a_string_enum = tagged_as<T>(parse_tag_e::string_enum);

template <typename T>
concept a_body = tagged_as<T>(parse_tag_e::body);

template <typename T>
concept an_operation = tagged_as<T>(parse_tag_e::operation);

template <typename T>
concept a_response = tagged_as<T>(parse_tag_e::response);

template <a_body T>
consteval std::string_view get_content_type() {
  static_assert(is_lowered(std::decay_t<T>::content_type));
  return std::decay_t<T>::content_type;
}

template <typename T>
constexpr bool has_encodings = requires(std::string_view str) {
  { T::encoding_for(str) };
};

template <an_operation O>
constexpr bool has_path = requires {
  O::path_expr;
  // O::visit_path(vtor)
};

template <an_operation O>
constexpr bool has_path_params = requires {
  typename O::path_t;
  O::path;
};

template <an_operation Op>
using path_t = typename std::decay_t<Op>::path_t;

template <an_operation O>
constexpr bool has_query_params = requires {
  typename O::query_t;
  O::query;
};

template <an_operation Op>
using query_t = typename std::decay_t<Op>::query_t;

template <an_operation O>
constexpr bool has_cookie_params = requires {
  typename O::cookies_t;
  O::cookies;
};

template <an_operation Op>
using cookies_t = typename std::decay_t<Op>::cookies_t;

template <typename O>
constexpr bool has_header_params = requires {
  typename O::headers_t;
  O::headers;
};

template <an_operation Op>
using headers_t = typename std::decay_t<Op>::headers_t;

template <typename T>
constexpr bool has_body = requires { std::decay_t<T>::body; };

template <typename T>
using body_t = decltype(std::declval<T>().body);

template <typename T>
constexpr bool has_single_body() noexcept {
  if constexpr (!has_body<T>)
    return false;
  else  // у variant/box_union не будет .body
    return has_parse_tag<svarg::noexport::deoptional_type_t<body_t<T>>>;
}

template <typename T>
constexpr bool has_many_bodies() noexcept {
  if constexpr (!has_body<T>)
    return false;
  else
    return !has_single_body<std::decay_t<T>>();
}

template <typename T>
[[nodiscard]] constexpr bool is_body_required() noexcept {
  if constexpr (!has_body<T>)
    return false;
  else {
    if constexpr (has_single_body<T>())
      return svarg::noexport::deoptional_type_t<body_t<T>>::is_required;  // TODO обработать опшнл
    else {
      return []<template <typename...> typename X, typename... Bodies>(X<std::monostate, Bodies...>) {
        return (Bodies::is_required && ...);
      }();
    }
  }
}

template <typename T>
[[nodiscard]] constexpr bool is_body_present(const T& op) noexcept {
  if constexpr (!has_body<T>)
    return false;
  else if constexpr (has_single_body<T>()) {
    if constexpr (is_body_required<T>())
      return true;
    else
      return !!op.body;
  } else {
    return !std::holds_alternative<std::monostate>(op.body);
  }
}

// ожидает что is_body_present(op) == true
template <typename T, typename Vtor>
constexpr void visit_body(T&& op, Vtor&& vtor) noexcept {
  static_assert(has_body<T>);
  if constexpr (has_single_body<T>()) {
    if constexpr (is_body_required<T>()) {
      std::forward<Vtor>(vtor)(noadl::forwardLike<T>(op.body));
      return;
    } else {
      if (!is_body_present(op))
        throw serialize_error("body for operation `{}` is required, but not present", type_name<T>());
      std::forward<Vtor>(vtor)(*noadl::forwardLike<T>(op.body));
      return;
    }
  } else {
    if constexpr (is_body_required<T>()) {
      if (!is_body_present(op))
        throw serialize_error("body for operation `{}` is required, but not present", type_name<T>());
    }
    std::visit(metel::matcher{
                   [](std::monostate) { metel::unreachable(); },
                   [&]<typename B>(B&& b) { std::forward<Vtor>(vtor)(noadl::forwardLike<T>(b)); },
               },
               std::forward_like<T>(op.body));
    return;
  }
}

// pre: `content_type` в нижнем регистре
// вызывает `vtor` передавая тип body которому соответствует `content_type`
// если такого нет - передаёт void
template <typename T, typename Vtor>
constexpr decltype(auto) select_body_type(std::string_view content_type, Vtor&& vtor) {
  static_assert(has_body<T>);

  assert(is_lowered(content_type));

  if constexpr (has_single_body<T>()) {
    using body_type = svarg::noexport::deoptional_type_t<body_t<T>>;

    if (get_content_type<body_type>() == content_type)
      return vtor.template operator()<body_type>();
    else
      return vtor.template operator()<void>();
  } else {
    return std::decay_t<T>::select_body_type(content_type, std::forward<Vtor>(vtor));
  }
}

// ожидает что is_body_present(op) == true
template <an_operation Op>
[[nodiscard]] constexpr std::string_view get_content_type(const Op& op) {
  static_assert(has_body<Op>);

  if constexpr (has_single_body<Op>()) {
    using body_type = svarg::noexport::deoptional_type_t<decltype(op.body)>;
    static_assert(!is_wildcard_content_type(get_content_type<body_type>()));
    return get_content_type<body_type>();
  } else {
    return std::visit(
        metel::matcher{
            [](std::monostate) -> std::string_view { throw serialize_error("body of operation is not set"); },
            []<typename B>(const B&) -> std::string_view {
              // звёздочки разрешены только в респонсах при определении что нужно парсить
              static_assert(!is_wildcard_content_type(get_content_type<B>()));
              return get_content_type<B>();
            },
        },
        op.body);
  }
}

// для cookie/headers/query
template <typename T>
std::string serialize_nonbody_into(const T& value) {
  if constexpr (std::is_same_v<T, std::string>)
    return value;
  else if constexpr (std::is_same_v<T, bool>)
    return value == true ? "true" : "false";
  else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>)
    return std::format("{}", value);
  else
    return boost::json::serialize(to_json(value));
}

template <content_type_e C, typename T>
[[nodiscard]] std::string serialize_into(const T& value) {
  if constexpr (C == content_type_e::application_json) {
    return boost::json::serialize(to_json(value));
  } else {
    static_assert(false, "unsupported content type");
  }
}

namespace noexport {

// код напрямую из boost json
// https://github.com/boostorg/json/blob/ea9066875c7f1bac807110e47c64df8e9ba876a7/include/boost/json/impl/serialize.ipp#L56
// но подходящий не только для std::string
inline void serialize_impl(auto& s, boost::json::serializer& sr) {
  // serialize to a small buffer to avoid
  // the first few allocations in std::string
  char buf[BOOST_JSON_STACK_BUFFER_SIZE];
  boost::json::string_view sv;
  sv = sr.read(buf);
  if (sr.done()) {
    // fast path
    s.append(sv.data(), sv.size());
    return;
  }
  size_t len = sv.size();
  s.reserve(len * 2);
  s.resize(s.capacity());
  BOOST_ASSERT(s.size() >= len * 2);
  std::memcpy(&s[0], sv.data(), sv.size());
  const auto lim = s.max_size() / 2;
  for (;;) {
    sv = sr.read(&s[0] + len, s.size() - len);
    len += sv.size();
    if (sr.done())
      break;
    // growth factor 2x
    if (s.size() < lim)
      s.resize(s.size() * 2);
    else
      s.resize(2 * lim);
  }
  s.resize(len);
}

}  // namespace noexport

template <a_body B>
[[nodiscard]] http_body_bytes serialize_body(const B& body, const content_type_params&) {
  constexpr content_type_e C = get_content_type<B>();
  if constexpr (C == content_type_e::application_json) {
    // калька кода из boost json serialize, но не для строки, а для http_body_bytes
    boost::json::value v = to_json(body);
    unsigned char tmpbuf[256];
    boost::json::serializer sr(boost::json::storage_ptr{}, +tmpbuf, sizeof(tmpbuf));
    http_body_bytes bytes;
    sr.reset(&v);
    noexport::serialize_impl(bytes, sr);
    return bytes;
  } else {
    static_assert(false, "unsupported content type");
  }
}

// парсинг для headers/cookies/queries которые не привязаны к конкретному content_type
template <typename T>
[[nodiscard]] error_code parse_nonbody_from(std::string_view str, T& out) {
  using U = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<U, std::string>) {
    out = str;
    return {};
  } else if constexpr (std::is_same_v<U, bool>) {
    if (str == "true")
      out = true;
    else if (str == "false")
      out = false;
    else
      return parse_error("cannot parse boolean from `{}`", str);
    return {};
  } else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>) {
    if (!try_from_chars(str, out))
      return parse_error("cannot parse `{}` from `{}`", type_name<U>(), str);
    return {};
  } else {
    boost::json::value v = boost::json::parse(str);
    return from_json(v, out);
  }
}

template <content_type_e C, typename T>
[[nodiscard]] error_code parse_from(std::string_view data, T& out) {
  if constexpr (C == content_type_e::application_json) {
    boost::json::value v = boost::json::parse(data);
    return try_from_json(v, out);
  } else {
    // starts_with_ci_prefix_lowered для тех у которых могут быть параметры
    static_assert(false, "unsupported content type");
  }
}

template <typename T>
constexpr bool has_additional_properties = [] {
  if constexpr (requires { typename T::additional_properties_type; })
    return !std::is_void_v<typename T::additional_properties_type>;
  else
    return false;
}();

template <size_t I, typename T>
[[nodiscard]] consteval std::string_view get_field_name() noexcept {
  return std::decay_t<T>::field_names[I];
}

template <typename T>
consteval size_t fields_count() {
  static_assert(metel::is_decayed_v<T>);
  if constexpr (cxx_support::an_object<T>) {
    constexpr bool ignore_last_field = cxx_support::an_object<T> && cxx_support::has_additional_properties<T>;
    return std::decay_t<T>::field_names.size() - size_t(ignore_last_field);
  } else if constexpr (std::is_aggregate_v<T>) {
    return boost::pfr::tuple_size_v<T>;
  } else {
    static_assert(false);
  }
}

template <size_t I, typename T>
[[nodiscard]] constexpr decltype(auto) get_field(T&& t) noexcept {
  return boost::pfr::get<I>(std::forward<T>(t));
}

template <size_t I, typename T>
[[nodiscard]] constexpr bool is_field_present(const T& t) noexcept {
  if constexpr (T::is_required(get_field_name<I, T>())) {
    return true;
  } else {
    return !!get_field<I>(t);
  }
}

// достаёт филд из box/optional, если он опциональный
template <size_t I, typename T>
constexpr decltype(auto) get_presented_field(T&& t) noexcept {
  using U = std::decay_t<T>;
  assert(is_field_present<U>(t));
  if constexpr (U::is_required(get_field_name<I, U>())) {
    return noadl::forwardLike<T>(get_field<I>(t));
  } else {
    return noadl::forwardLike<T>(*get_field<I>(t));
  }
}

}  // namespace svarg::cxx_support
