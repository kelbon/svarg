#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

void ir_object::merge(ir_object other) {
  for (auto&& t : other.types) {
    if (!std::ranges::contains(types, t))
      types.push_back(std::move(t));
  }
}

bool type_ref::refers_to(const type& t) const noexcept {
  if (fundamental) {
    assert(*fundamental != fundamental_e::array && *fundamental != fundamental_e::object);
    auto* ft = std::get_if<fundamental_e>(&t.data);
    return ft && *fundamental == *ft;
  }
  return name == t.name && scope == t.scope;
}

}  // namespace svarg::ir
