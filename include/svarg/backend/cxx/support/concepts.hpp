#pragma once

#include <format>
#include <optional>
#include <string_view>
#include <concepts>

#include <strswitch/strswitch.hpp>

#include <boost/pfr.hpp>

#include <metel/meta.hpp>

#include <kelcoro/generator.hpp>
#include "svarg/backend/cxx/support/types.hpp"

// описание генерируемых по умолчанию структур - операций и схем

namespace svarg::cxx_support {

enum struct parse_tag_e {
  // просто комбинаторы, недостаточно тривиальные чтобы сделать их алиасами

  // имя "спецполя" = <special_field_name>
  // у каждого может быть using required_schemas = typelist<...>;

  array,  // одно спецполе array
  anyof,  // одно спецполе anyof + возможен discriminator_field + discriminate(string_view, auto&& vtor)
  oneof,  // одно спецполе anyof + возможен discriminator_field + discriminate(string_view, auto&& vtor)
  allof,  // одно спецполе - allof с именем <special_field_name>
  not_a,  // одно спецполе

  // не комбинаторы
  object,  // сначала требуемые поля, затем опциональные + is_required(string_view) +
           // additional_properties_type / additional_properties
  enum_,
  string_enum,

  body,  // string_view content_type + <special_field_name> единственное поле с данными +
         // encoding_for(string_view)
};

constexpr std::string_view e2str(parse_tag_e e) noexcept {
  switch (e) {
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
  }
  std::unreachable();
}

[[nodiscard]] inline bool efromstr(std::string_view s, parse_tag_e& out) {
  std::optional t = ss::string_switch<parse_tag_e>(s)
                        .case_("array", parse_tag_e::array)
                        .case_("anyof", parse_tag_e::anyof)
                        .case_("oneof", parse_tag_e::oneof)
                        .case_("allof", parse_tag_e::allof)
                        .case_("not_a", parse_tag_e::not_a)
                        .case_("object", parse_tag_e::object)
                        .case_("enum_", parse_tag_e::enum_)
                        .case_("string_enum", parse_tag_e::string_enum)
                        .case_("body", parse_tag_e::body)
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

template <a_body T>
consteval std::string_view get_content_type() {
  return std::decay_t<T>::content_type;
}

template <typename T>
constexpr bool has_encodings = requires(std::string_view str) {
  { T::encoding_for(str) };
};

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

// TODO вместо этого нужно просто to_json_string и по adl вызывать таким же образом остальное (например
// to_xml_string)
// преобразует схему в токены для сериализации не зависящей от выходного формата, json/xml/...
/// template <typename T>
/// struct into_tokens {
///   // void operator()(const T&, serialize_tokens_consumer auto&);
/// };

// TODO видимо надо специальный тип который сохраняет токены ЕСЛИ НАДО для повторного прохода

// TODO все эти штуки на самом деле не знают откуда им придётся парсится. json/xml/query и так далее
// так что нужно сюда переносить omo и парсинг организовывать на этом + видимо исключения не бросать, хотя
// хз, пофиг на Not

// сериализация в обратную сторону должна быть не привязана ни к чему. Вроде в тгбм +- так сделано для боди
// это стоит заимствовать.
// а в целом для всего нужно превратить штуки в наборы токенов, которые потом для json/xml/query и прочего

// TODO поддерживать форматы. Возможно наконец прокинуть со входа "известные форматы" и по сути во что они
// маппятся на уровне кодгена и они просто маппят тип + формат в какой-то тип конкретного языка
// программирования
// Хотя по сути поддержка формата это выдача варнинга на незнакомый на фронтенде + присутствие информации о
// нём на бекенде в виде типа или отдельно поля формат (для фундаментального видимо) + для параметров стиль
// видимо прокидывать

// TODO условное serialize/parse parameter должно принимать стиль/формат помимо самой штуки

}  // namespace svarg::cxx_support
