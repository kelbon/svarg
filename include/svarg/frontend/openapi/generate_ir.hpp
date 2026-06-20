#pragma once

#include "svarg/ir/ir_object.hpp"
#include "svarg/frontend/openapi/openapi.hpp"

namespace svarg {

// просто переводит в IR без каких-либо оптимизирующих пассов
[[nodiscard]] ir::ir_object openapi2ir_raw(fs::path specsdir, std::vector<fs::path> specs2generate,
                                           openapi_options options = {});

// переводит в IR и оптимизирует
[[nodiscard]] ir::ir_object openapi2ir(fs::path specsdir, std::vector<fs::path> specs2generate,
                                       openapi_options options = {});

// заменяет все тривиальные схемы (с фундаментальными типами) на
// фундаментальные ссылки, упрощая IR
[[nodiscard]] ir::ir_object pass_rm_trivial_schemas(ir::ir_object);

// убирает все схемы, которые не используются в операциях/вебхуках (кроме тех что в exceptions)
// применяется не всегда (так как иногда такие схемы нужно оставить для использования кодом)
[[nodiscard]] ir::ir_object pass_rm_unused_schemas(ir::ir_object,
                                                   std::unordered_set<ir::schema_ref> exceptions = {});

}  // namespace svarg
