#pragma once

#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

// заменяет все тривиальные схемы (с фундаментальными типами) на
// фундаментальные ссылки, упрощая IR
[[nodiscard]] ir_object pass_rm_trivial_schemas(ir_object);

// убирает все схемы, которые не используются в операциях/вебхуках (кроме тех что в exceptions)
// применяется не всегда (так как иногда такие схемы нужно оставить для использования кодом)
[[nodiscard]] ir_object pass_rm_unused_schemas(ir_object, std::unordered_set<schema_ref> exceptions = {});

}  // namespace svarg::ir
