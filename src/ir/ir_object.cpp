#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

void ir_object::merge(ir_object other) {
  if (other.schemas) {
    if (!schemas)
      schemas.emplace();
    schemas->merge(std::move(*other.schemas));
  }
  if (other.operations) {
    if (!operations)
      operations.emplace();
    operations->merge(std::move(*other.operations));
  }
  if (other.webhooks) {
    if (!webhooks)
      webhooks.emplace();
    webhooks->merge(std::move(*other.webhooks));
  }
}

bool schema_ref::refers_to(const schema& t) const noexcept {
  return spec == t.spec && jsonptr == t.jsonptr;
}

}  // namespace svarg::ir
