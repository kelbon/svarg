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
  object,  // сначала требуемые поля, затем опциональные + fields (описания полей) +
           // additional_properties_type / additional_properties
  enum_,
  string_enum,

  body,       // string_view content_type + <special_field_name> единственное поле с данными
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

// описание поля объекта или структуры параметров операции (path_t, query_t, headers_t, cookies_t).
// Описания лежат в `static constexpr std::array<field_info, N> fields` в порядке объявления полей
struct field_info {
  // имя из спецификации, C++ имя поля может отличаться
  std::string_view name;
  bool required = false;
  // медиа тип значения: `content:` у параметра, encoding свойства form/multipart тела. Пусто - по умолчанию
  std::string_view encoding = {};
  // параметр-массив/объект: каждый элемент отдельно (ids=1&ids=2). nullopt - по умолчанию для места
  // параметра (см. param_style.hpp)
  std::optional<bool> explode = std::nullopt;
};

template <typename T>
concept has_named_fields = requires {
  { T::fields[0] } -> std::convertible_to<const field_info&>;
} || requires { requires T::fields.size() == 0; };

template <a_body T>
consteval std::string_view get_content_type() {
  static_assert(is_lowered(std::decay_t<T>::content_type));
  return std::decay_t<T>::content_type;
}

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

// операции и ответы
template <typename T>
using headers_t = typename std::decay_t<T>::headers_t;

template <typename T>
constexpr bool has_body = requires { std::decay_t<T>::body; };

// единственное тело: `body` имеет тип самой схемы, а content type и прочее - статические члены владельца
// body_content_type, body_is_required. Несколько тел: variant<monostate, тела...>, где каждое тело
// a_body со своим content_type
template <typename T>
constexpr bool has_single_body() noexcept {
  return requires { std::decay_t<T>::body_content_type; };
}

template <typename T>
[[nodiscard]] constexpr bool is_body_required() noexcept {
  if constexpr (!has_body<T>)
    return false;
  else if constexpr (has_single_body<T>())
    return std::decay_t<T>::body_is_required;
  else {
    return []<template <typename...> typename X, typename... Bodies>(
               std::type_identity<X<std::monostate, Bodies...>>) {
      return (Bodies::is_required && ...);
    }(std::type_identity<std::decay_t<decltype(std::declval<T>().body)>>{});
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

template <typename T>
constexpr bool has_additional_properties = [] {
  if constexpr (requires { typename T::additional_properties_type; })
    return !std::is_void_v<typename T::additional_properties_type>;
  else
    return false;
}();

template <size_t I, typename T>
[[nodiscard]] consteval const field_info& get_field_info() noexcept {
  return std::decay_t<T>::fields[I];
}

template <size_t I, typename T>
[[nodiscard]] consteval std::string_view get_field_name() noexcept {
  return get_field_info<I, T>().name;
}

// медиа тип значения поля, пусто если не задан или поля не описаны
template <size_t I, typename T>
[[nodiscard]] consteval std::string_view get_field_encoding() noexcept {
  if constexpr (has_named_fields<std::decay_t<T>>)
    return get_field_info<I, T>().encoding;
  else
    return {};
}

// сгенерированные типы перечисляют поля сами (fields, svarg_field<I>, единственное поле _implfield),
// boost::pfr - только для написанных вручную структур: он дорог для компиляции
template <typename T>
constexpr bool has_generated_fields = requires { requires T::has_svarg_field; };

template <typename T>
constexpr bool has_single_value_field = requires(T& t) { t._implfield; };

template <typename T>
consteval size_t fields_count() {
  static_assert(metel::is_decayed_v<T>);
  if constexpr (has_named_fields<T>) {
    // fields - только объявленные свойства, поле additional properties (последнее) в них не входит
    return std::decay_t<T>::fields.size();
  } else if constexpr (has_single_value_field<T>) {
    return 1;
  } else if constexpr (std::is_aggregate_v<T>) {
    return boost::pfr::tuple_size_v<T>;
  } else {
    static_assert(false);
  }
}

template <size_t I, typename T>
[[nodiscard]] constexpr decltype(auto) get_field(T&& t) noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (has_generated_fields<U>) {
    return std::forward<T>(t).template svarg_field<I>();
  } else if constexpr (has_single_value_field<U> && !has_named_fields<U>) {
    static_assert(I == 0);
    return noadl::forwardLike<T>(t._implfield);
  } else {
    return boost::pfr::get<I>(std::forward<T>(t));
  }
}

}  // namespace svarg::cxx_support
