#pragma once

#include "svarg/ir/options.hpp"

namespace svarg::ir {

[[nodiscard]] ir_object optimization_pipeline(ir_object, const ir_options&);

// заменяет все тривиальные схемы (с фундаментальными типами) на
// фундаментальные ссылки, упрощая IR
[[nodiscard]] ir_object pass_rm_trivial_schemas(ir_object);

// убирает все схемы, которые не используются в операциях/вебхуках и других схемах
// (кроме тех что в exceptions)
// применяется не всегда (так как иногда такие схемы нужно оставить для использования кодом)
[[nodiscard]] ir_object pass_rm_unused_schemas(ir_object, const ir_options&);

// преобразует anyOf<a, anyOf<b, c>, d> в anyOf<a, b, c, d>
// и аналогично для allOf, oneOf и заодно not<not<X>> -> X
// также убирает дубликаты в их списках и преобразует anyOf<X> => X
// и аналогично для allOf, oneOf
[[nodiscard]] ir_object pass_combinators_flatten(ir_object, const ir_options&);

// удаляет требования, которые ничего не добавляют (не удаляет сами схемы требований, если они стали
// неиспользуемымми)
[[nodiscard]] ir_object pass_remove_useless_requirements(ir_object, const ir_options&);

}  // namespace svarg::ir
