#pragma once

#include "svarg/ir/ir_object.hpp"
#include "svarg/frontend/openapi/openapi.hpp"

namespace svarg {

[[nodiscard]] ir::ir_object openapi2ir(fs::path specsdir, std::vector<fs::path> specs2generate,
                                       openapi_options options = {});

}
