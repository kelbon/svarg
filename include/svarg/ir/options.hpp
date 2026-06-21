#pragma once

#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

struct ir_options {
  bool allow_rm_unused = true;
  bool enable_optimizations = true;
  // спека + путь до типов которые не должны удалиться даже если они unused
  // (имеет смысл только с allow_rm_unused = true)
  std::unordered_set<schema_ref> marked_used;
};

}  // namespace svarg::ir
