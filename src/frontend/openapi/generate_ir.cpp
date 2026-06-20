#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/frontend/openapi/lowering_context.hpp"
#include "svarg/ir/ir_visitor.hpp"

#include <algorithm>
#include <ranges>

namespace svarg {

ir::ir_object openapi2ir_raw(fs::path specsdir, std::vector<fs::path> specs2generate,
                             openapi_options options) {
  lowering_context ctx;
  return ctx.generate_specs(std::move(specsdir), std::move(specs2generate), std::move(options));
}

ir::ir_object openapi2ir(fs::path specsdir, std::vector<fs::path> specs2generate, openapi_options options) {
  ir::ir_object ir = openapi2ir_raw(std::move(specsdir), std::move(specs2generate), std::move(options));
  ir = pass_rm_trivial_schemas(std::move(ir));
  return ir;
}

template <typename Foo>
struct schema_ref_vtor : ir::ir_visitor {
  Foo f;

  schema_ref_vtor(Foo f) : f(std::move(f)) {
  }

  using ir::ir_visitor::operator();

  void operator()(const ir::schema_ref& s) {
    // схемы не влияют на кеш и сравнения в unordered сетах, поэтому const cast здесь нормален
    f(const_cast<ir::schema_ref&>(s));
  }
};

ir::ir_object pass_rm_trivial_schemas(ir::ir_object obj) {
  if (!obj.schemas)
    return obj;
  auto it = obj.schemas->begin();
  auto e = obj.schemas->end();
  for (; it != e;) {
    auto& s = *it;
    if (s.is_trivial()) {
      ir::schema_ref replace = ir::make_fundamental_ref(std::get<ir::fundamental_e>(s.type.data));
      auto vtor = [&](ir::schema_ref& r) {
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

ir::ir_object pass_rm_unused_schemas(ir::ir_object ir, std::unordered_set<ir::schema_ref> exceptions) {
  if (!ir.schemas)
    return ir;
  std::unordered_set<ir::schema_ref> used = std::move(exceptions);
  auto vtor = [&](const ir::schema_ref& r) { used.insert(r); };
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

}  // namespace svarg
