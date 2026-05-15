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
             .orDefault(name);
  if (name.ends_with('_'))
    throw "may be i forget some field";
  return name;
}

}  // namespace svarg
