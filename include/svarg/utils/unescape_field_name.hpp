#pragma once

#include <string_view>
#include <strswitch/strswitch.hpp>

namespace svarg {

// принимает имя поля из C++ структуры и возвращает то, которое было в спеке и должно быть в json
//
consteval std::string_view unescape_field_name(std::string_view name) {
  name = ss::string_switch<std::string_view>(name)
             .case_("default_", "default")
             .case_("namespace_", "namespace")
             .case_("enum_", "enum")
             .case_("delete_", "delete")
             .case_("ref_", "$ref")
             .case_("const_", "const")
             .case_("not_", "not")
             .case_("if_", "if")
             .case_("else_", "else")
             .or_default(name);
  // чтобы не забыть _responses в responses_object и подобные вещи.
  // По сути ключевые слова которые нужно специальным образом обработать
  if (name.starts_with('_') && !name.starts_with("_keyfield"))
    throw "special field not handled correctly";
  if (name.ends_with('_'))
    throw "may be i forget some field";
  return name;
}

}  // namespace svarg
