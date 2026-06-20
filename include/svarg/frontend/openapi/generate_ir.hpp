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

}  // namespace svarg
