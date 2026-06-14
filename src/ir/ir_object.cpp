#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

void ir_object::merge(ir_object other) {
  schemas.merge(std::move(other.schemas));
  operations.merge(std::move(other.operations));
  webhooks.merge(std::move(other.webhooks));
}

bool schema_ref::refers_to(const schema& t) const noexcept {
  return spec == t.spec && jsonptr == t.jsonptr;
}

}  // namespace svarg::ir
