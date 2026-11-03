#pragma once

// имена и теги, общие для рантайма и генератора. Без hidi: генератор их использует, а hidi ему не нужен

#include <optional>
#include <string_view>

#include <metel/meta.hpp>
#include <strswitch/strswitch.hpp>

namespace svarg::cxx_support {

constexpr inline std::string_view cookie_hdr = "cookie";

constexpr inline std::string_view content_type_hdr = "content-type";

// имя максимально кривое для наименьшей вероятности пересечения с другими именами
static constexpr std::string_view special_field_name = "svarg_implfield";

constexpr inline std::string_view additional_properties_schema_alias_name = "svarg_additional_properties_type";
constexpr inline std::string_view additional_properties_field_name = "svarg_additional_properties";
constexpr inline std::string_view additional_properties_forbidden_name = "svarg_additional_properties_forbidden";

enum struct parse_tag_e {
  // просто комбинаторы, недостаточно тривиальные чтобы сделать их алиасами

  // имя "спецполя" = <special_field_name>
  // у каждого может быть using svarg_required_schemas = typelist<...>;

  fundamental,  // какой-то фундаментал с ограничениями
  array,        // одно спецполе array
  anyof,        // одно спецполе anyof + возможен svarg_discriminator_field + svarg_discriminate(string_view, auto&& vtor)
  oneof,        // одно спецполе anyof + возможен svarg_discriminator_field + svarg_discriminate(string_view, auto&& vtor)
  allof,        // одно спецполе - allof с именем <special_field_name>
  not_a,        // одно спецполе

  // не комбинаторы
  object,  // сначала требуемые поля, затем опциональные + fields (описания полей) +
           // svarg_additional_properties_type / svarg_additional_properties
  enum_,
  string_enum,
  open_string_enum,  // std::string value + известные значения values, допустима любая строка

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
    case parse_tag_e::open_string_enum:
      return "open_string_enum";
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
                        .case_("open_string_enum", parse_tag_e::open_string_enum)
                        .case_("body", parse_tag_e::body)
                        .case_("operation", parse_tag_e::operation)
                        .case_("response", parse_tag_e::response)
                        .or_nullopt();
  if (!t)
    return false;
  out = *t;
  return true;
}

}  // namespace svarg::cxx_support
