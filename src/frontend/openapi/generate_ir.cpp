#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/frontend/openapi/lowering_context.hpp"

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

static void visit_schemas_in_type(auto&& vtor, ir::schema_type& type) {
  visit_one(
      type.data,
      [&](ir::enum_schema& e) {
        if (e.type)
          vtor(*e.type);
      },
      // TODO discriminator
      [&](ir::oneof_schema& oof) {
        if (oof.discriminator)
          std::ranges::for_each(oof.discriminator->mapping | std::views::values, vtor);
        std::ranges::for_each(oof.schemas, vtor);
      },
      [&](ir::anyof_schema& aof) { std::ranges::for_each(aof.schemas, vtor); },
      [&](ir::allof_schema& alof) { std::ranges::for_each(alof.schemas, vtor); },
      [&](ir::object_schema& obj) {
        std::ranges::for_each(obj.fields | std::views::transform(&ir::object_field_info::type), vtor);
        if (obj.additional_fields)
          vtor(*obj.additional_fields);
      },
      [&](ir::array_schema& arr) { vtor(arr.element); }, [&](ir::not_schema& no) { vtor(no.type); },
      [](ir::fundamental_e&) {});
}

static void visit_schemas_in_body(auto&& vtor, ir::body& b) {
  vtor(b.schema);
}

static void visit_schemas_in_response(auto&& vtor, ir::response_desc& resp) {
  if (resp.bodies) {
    for (auto& _b : *resp.bodies) {
      ir::body& b = const_cast<ir::body&>(_b);
      vtor(b.schema);
    }
  }
  if (resp.headers) {
    for (auto& _h : *resp.headers) {
      ir::header& h = const_cast<ir::header&>(_h);
      if (h.schema)
        vtor(*h.schema);
    }
  }
}

static void visit_schemas_in_parameter(auto&& vtor, ir::operation_parameter& param) {
  vtor(param.schema);
}

static void visit_schemas_in_operation(auto&& vtor, ir::operation& op) {
  if (op.responses) {
    for (auto& rsp : *op.responses)
      visit_schemas_in_response(vtor, const_cast<ir::response_desc&>(rsp));
  }
  if (op.defaultResponse)
    visit_schemas_in_response(vtor, const_cast<ir::response_desc&>(*op.defaultResponse));
  if (op.bodies) {
    for (auto& b : *op.bodies) {
      visit_schemas_in_body(vtor, const_cast<ir::body&>(b));
    }
  }
  if (op.parameters)
    for (auto& param : *op.parameters)
      visit_schemas_in_parameter(vtor, param);
}

template <bool VisitSchemas>
static void visit_all_schema_refs(auto&& vtor, ir::ir_object& obj) {
  assert(obj.schemas);

  if constexpr (VisitSchemas) {
    for (auto& _s : *obj.schemas) {
      // меняем только то, что не влияет хеш и сравнения
      ir::schema& s = const_cast<ir::schema&>(_s);
      if (s.required)
        std::ranges::for_each(*s.required, vtor);
      visit_schemas_in_type(vtor, s.type);
    }
  }
  if (obj.operations) {
    for (auto& _op : *obj.operations) {
      // меняем только то, что не влияет хеш и сравнения
      ir::operation& op = const_cast<ir::operation&>(_op);
      visit_schemas_in_operation(vtor, op);
    }
  }
  if (obj.webhooks) {
    for (auto& _wh : *obj.webhooks) {
      // меняем только то, что не влияет хеш и сравнения
      ir::operation& op = const_cast<ir::operation&>(_wh);
      visit_schemas_in_operation(vtor, op);
    }
  }
}

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
      visit_all_schema_refs</*VisitSchemas=*/true>(vtor, obj);
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
  visit_all_schema_refs</*VisitSchemas=*/false>(vtor, ir);
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
