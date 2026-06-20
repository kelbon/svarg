#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/frontend/openapi/lowering_context.hpp"

#include "svarg/ir/optimization_passes.hpp"

namespace svarg {

ir::ir_object openapi2ir_raw(fs::path specsdir, std::vector<fs::path> specs2generate,
                             openapi_options options) {
  lowering_context ctx;
  return ctx.generate_specs(std::move(specsdir), std::move(specs2generate), std::move(options));
}

ir::ir_object openapi2ir(fs::path specsdir, std::vector<fs::path> specs2generate, openapi_options options) {
  ir::ir_object ir = openapi2ir_raw(std::move(specsdir), std::move(specs2generate), std::move(options));
  ir = ir::pass_rm_trivial_schemas(std::move(ir));
  return ir;
}

}  // namespace svarg
