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
  }
  metel::unreachable();
}

[[nodiscard]] inline bool efromstr(std::string_view s, parse_tag_e& out) {
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

template <a_body T>
consteval std::string_view get_content_type() {
  return std::decay_t<T>::content_type;
}

template <typename T>
constexpr bool has_encodings = requires(std::string_view str) {
  { T::encoding_for(str) };
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

template <an_operation O>
constexpr bool has_header_params = requires {
  typename O::headers_t;
  O::headers;
};

template <an_operation Op>
using headers_t = typename std::decay_t<Op>::headers_t;

template <an_operation Op>
constexpr bool has_body = requires { Op::body; };

template <an_operation Op>
using body_t = decltype(std::declval<Op>().body);

template <an_operation Op>
constexpr bool has_single_body() noexcept {
  if constexpr (!has_body<Op>)
    return false;
  else  // у variant/box_union не будет .body
    return has_parse_tag<svarg::noexport::deoptional_type_t<body_t<Op>>>;
}

template <an_operation Op>
constexpr bool has_many_bodies() noexcept {
  if constexpr (!has_body<Op>)
    return false;
  else
    return !has_single_body<Op>();
}

template <an_operation Op>
[[nodiscard]] constexpr bool is_body_required() noexcept {
  if constexpr (!has_body<Op>)
    return false;
  else {
    if constexpr (has_single_body<Op>())
      return svarg::noexport::deoptional_type_t<body_t<Op>>::is_required;  // TODO обработать опшнл
    else {
      return []<template <typename...> typename X, typename... Bodies>(X<std::monostate, Bodies...>) {
        return (Bodies::is_required && ...);
      }();
    }
  }
}

template <an_operation Op>
[[nodiscard]] constexpr bool is_body_present(const Op& op) noexcept {
  if constexpr (!has_body<Op>)
    return false;
  else if constexpr (has_single_body<Op>()) {
    if constexpr (is_body_required<Op>())
      return true;
    else
      return !!op.body;
  } else {
    return !std::holds_alternative<std::monostate>(op.body);
  }
}

// ожидает что is_body_present(op) == true
template <an_operation Op, typename Vtor>
constexpr void visit_body(Op&& op, Vtor&& vtor) noexcept {
  static_assert(has_body<Op>);
  if constexpr (has_single_body<Op>()) {
    if constexpr (is_body_required<Op>()) {
      std::forward<Vtor>(vtor)(noadl::forwardLike<Op>(op.body));
      return;
    } else {
      if (!is_body_present(op))
        throw serialize_error("body for operation `{}` is required, but not present", type_name<Op>());
      std::forward<Vtor>(vtor)(*noadl::forwardLike<Op>(op.body));
      return;
    }
  } else {
    if constexpr (is_body_required<Op>()) {
      if (!is_body_present(op))
        throw serialize_error("body for operation `{}` is required, but not present", type_name<Op>());
    }
    std::visit(metel::matcher{
                   [](std::monostate) { metel::unreachable(); },
                   [&]<typename B>(B&& b) { std::forward<Vtor>(vtor)(noadl::forwardLike<Op>(b)); },
               },
               std::forward_like<Op>(op.body));
    return;
  }
}

// ожидает что is_body_present(op) == true
template <an_operation Op>
[[nodiscard]] constexpr std::string_view get_content_type(const Op& op) {
  static_assert(has_body<Op>);

  if constexpr (has_single_body<Op>()) {
    return get_content_type<svarg::noexport::deoptional_type_t<decltype(op.body)>>();
  } else {
    return std::visit(
        metel::matcher{
            [](std::monostate) -> std::string_view { throw serialize_error("body of operation is not set"); },
            []<typename B>(const B&) -> std::string_view { return get_content_type<B>(); },
        },
        op.body);
  }
}

template <typename T>
concept an_application_json_body = a_body<T> && get_content_type<T>() == "application/json";

static_assert(!an_object<int>);

template <typename T>
concept serialize_tokens_consumer =
    requires(T& v, std::string_view s, int64_t i, uint64_t ui, double d, bool b) {
      v.on_array_begin();
      v.on_array_end();
      v.on_object_begin();
      v.on_object_end();
      v.on_string(s);
      v.on_key(s);
      v.on_int64(i);
      v.on_uint64(ui);
      v.on_double(d);
      v.on_bool(b);
      v.on_null();
    };

enum struct content_type_e {
  application_json,
  application_problem_json,
  multipart_related,  // TODO реально саппорт, а не просто тут
};

inline constexpr std::string_view e2str(content_type_e c) noexcept {
  switch (c) {
    case content_type_e::application_json:
      return "application/json";
    case content_type_e::application_problem_json:
      return "application/problem+json";
    case content_type_e::multipart_related:
      return "multipart/related";
  }
  metel::unreachable();
}

inline constexpr bool efromstr(std::string_view str, content_type_e& out) noexcept {
  std::optional c = ss::string_switch<content_type_e>(str)
                        .case_("application/json", content_type_e::application_json)
                        .case_("application/problem+json", content_type_e::application_problem_json)
                        .case_("multipart/related", content_type_e::multipart_related)
                        .or_nullopt();
  if (!c)
    return false;
  out = *c;
  return true;
}

template <content_type_e C, typename T>
[[nodiscard]] std::string serialize_into(const T& value) {
  if constexpr (C == content_type_e::application_json) {
    if constexpr (std::is_same_v<T, std::string>)
      return value;
    else if constexpr (std::is_same_v<T, bool>)
      return value == true ? "true" : "false";
    else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>)
      return std::format("{}", value);
    else
      return boost::json::serialize(to_json(value));
  } else {
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
  constexpr bool ignore_last_field = cxx_support::an_object<T> && cxx_support::has_additional_properties<T>();
  return std::decay_t<T>::field_names.size() - size_t(ignore_last_field);
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
