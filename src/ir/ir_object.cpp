#include "svarg/ir/ir_object.hpp"
#include "svarg/ir/ir_visitor.hpp"

namespace svarg::ir {

void ir_object::validate() const {
  if (schemas) {
    bool ok = true;
    auto foo = [&](const schema_ref& r) {
      if (!r.is_fundamental() && !schemas->contains(r))
        throw validate_error("ir references to {}#{} not found in ir", r.spec,
                             r.jsonptr ? r.jsonptr->escaped() : std::string_view{});
    };
    schema_ref_vtor checker(foo);
    checker.visit(*this);
  }
}

// для удобных ассёртов
bool validate_ir_object(const ir_object& ir) {
  try {
    ir.validate();
    return true;
  } catch (...) {
    return false;
  }
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
