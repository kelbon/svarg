#include "svarg/ir/optimization_passes.hpp"
#include "svarg/ir/ir_visitor.hpp"

namespace svarg::ir {

template <typename Foo>
struct schema_ref_vtor : ir_visitor {
  Foo f;

  schema_ref_vtor(Foo f) : f(std::move(f)) {
  }

  using ir_visitor::operator();

  void operator()(this auto&& self, const schema_ref& s) {
    // схемы не влияют на кеш и сравнения в unordered сетах, поэтому const cast здесь нормален
    self.f(const_cast<schema_ref&>(s));
  }
};

ir_object pass_rm_trivial_schemas(ir_object obj) {
  if (!obj.schemas)
    return obj;
  auto it = obj.schemas->begin();
  auto e = obj.schemas->end();
  for (; it != e;) {
    auto& s = *it;
    if (s.is_trivial()) {
      schema_ref replace = make_fundamental_ref(std::get<fundamental_e>(s.type.data));
      auto vtor = [&](schema_ref& r) {
        if (r.refers_to(s))
          r = replace;
      };
      schema_ref_vtor replacer(vtor);
      replacer.visit(obj);
      it = obj.schemas->erase(it);
    } else {
      ++it;
    }
  }
  return obj;
}

ir_object pass_rm_unused_schemas(ir_object ir, std::unordered_set<schema_ref> exceptions) {
  if (!ir.schemas)
    return ir;
  std::unordered_set<schema_ref> used = std::move(exceptions);
  auto vtor = [&](const schema_ref& r) { used.insert(r); };
  schema_ref_vtor collector(vtor);
  collector.visit(ir.operations);
  collector.visit(ir.webhooks);

  auto it = ir.schemas->begin();
  auto e = ir.schemas->end();
  for (; it != e;) {
    if (!used.contains(it->ref()))
      it = ir.schemas->erase(it);
    else
      ++it;
  }
  return ir;
}

}  // namespace svarg::ir
