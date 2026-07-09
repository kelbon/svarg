#pragma once

#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

enum struct remove_category_e {
  noone = 0,            // не удалять никого
  only_inline_schemas,  // те, у которых нет имён
  any,                  // какая угодно неспользуемая схема
};

struct ir_options {
  bool enable_optimizations = true;
  remove_category_e allow_rm_unused = remove_category_e::only_inline_schemas;
  // спека + путь до типов которые не должны удалиться даже если они unused
  // (имеет смысл только с allow_rm_unused = true)
  std::unordered_set<schema_ref> marked_used;
};

}  // namespace svarg::ir
