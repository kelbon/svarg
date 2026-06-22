#include "svarg/ir/ir_object.hpp"
#include "svarg/ir/ir_visitor.hpp"

namespace svarg::ir {

bool validate_ir_object(ir_object& ir) {
  if (ir.schemas) {
    bool ok = true;
    auto foo = [&](const schema_ref& r) {
      if (!r.is_fundamental() && !ir.schemas->contains(r))
        ok = false;
    };
    schema_ref_vtor checker(foo);
    checker.visit(ir);
    if (!ok)
      return ok;
  }
  return true;
}

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
