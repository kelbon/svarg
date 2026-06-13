#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/frontend/openapi/lowering_context.hpp"

namespace svarg {

ir::ir_object openapi2ir(fs::path specsdir, std::vector<fs::path> specs2generate, openapi_options options) {
  lowering_context ctx;
  return ctx.generate_specs(std::move(specsdir), std::move(specs2generate), std::move(options));
}

}  // namespace svarg
