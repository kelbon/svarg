#pragma once

#include "svarg/ir/ir_object.hpp"

namespace svarg::cxx {

struct render_options {
  // если `false`, то алгоритм преобразования строк в cxx идентификаторы
  // будет гарантировать несовпадение имён, но имена будут иногда более сложные
  bool simple_cxx_identifiers = true;
};

std::string render_type(const ir::schema_type&, const std::unordered_set<ir::schema>& schemas,
                        std::vector<ir::schema_ref>& mentioned, const render_options&);

}  // namespace svarg::cxx
