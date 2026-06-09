#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

void ir_object::merge(ir_object other) {
  schemas.merge(std::move(other.schemas));
  operations.merge(std::move(other.operations));
  webhooks.merge(std::move(other.webhooks));
}

bool schema_ref::refers_to(const schema& t) const noexcept {
  if (fundamental) {
    assert(*fundamental != fundamental_e::array && *fundamental != fundamental_e::object);
    auto* ft = std::get_if<fundamental_e>(&t.data);
    return ft && *fundamental == *ft;
  }
  return name == t.name && scope == t.scope;
}

}  // namespace svarg::ir
