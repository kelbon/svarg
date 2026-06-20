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

static void replace_in_type(const ir::schema& what, ir::schema_ref replace, ir::schema_type& type) {
  ir::schema_ref old = what.ref();
  visit_one(
      type.data,
      [&](ir::enum_schema& e) {
        if (e.type->refers_to(what))
          e.type = replace;
      },
      [&](ir::oneof_schema& oof) { std::ranges::replace(oof.schemas, old, replace); },
      [&](ir::anyof_schema& aof) { std::ranges::replace(aof.schemas, old, replace); },
      [&](ir::allof_schema& alof) { std::ranges::replace(alof.schemas, old, replace); },
      [&](ir::object_schema& obj) {
        std::ranges::replace(obj.fields | std::views::transform(&ir::object_field_info::type), old, replace);
        if (obj.additional_fields && obj.additional_fields->refers_to(what))
          obj.additional_fields = replace;
      },
      [&](ir::array_schema& arr) {
        if (arr.element == old)
          arr.element = replace;
      },
      [&](ir::not_schema& no) {
        if (no.type == old)
          no.type = replace;
      },
      [](ir::fundamental_e&) {});
}

static void replace_in_body(const ir::schema& what, ir::schema_ref replace, ir::body& b) {
  if (b.schema.refers_to(what))
    b.schema = replace;
}

static void replace_in_response(const ir::schema& what, ir::schema_ref replace, ir::response_desc& resp) {
  if (resp.bodies) {
    for (auto& _b : *resp.bodies) {
      ir::body& b = const_cast<ir::body&>(_b);
      if (b.schema.refers_to(what))
        b.schema = replace;
    }
  }
  if (resp.headers) {
    for (auto& _h : *resp.headers) {
      ir::header& h = const_cast<ir::header&>(_h);
      if (h.schema->refers_to(what))
        h.schema = replace;
    }
  }
}

static void replace_in_parameter(const ir::schema& what, ir::schema_ref replace,
                                 ir::operation_parameter& param) {
  if (param.schema.refers_to(what))
    param.schema = replace;
}

static void replace_in_operation(const ir::schema& what, ir::schema_ref replace, ir::operation& op) {
  if (op.responses) {
    for (auto& rsp : *op.responses)
      replace_in_response(what, replace, const_cast<ir::response_desc&>(rsp));
  }
  if (op.defaultResponse)
    replace_in_response(what, replace, const_cast<ir::response_desc&>(*op.defaultResponse));
  if (op.bodies) {
    for (auto& b : *op.bodies) {
      replace_in_body(what, replace, const_cast<ir::body&>(b));
    }
  }
  if (op.parameters)
    for (auto& param : *op.parameters)
      replace_in_parameter(what, replace, param);
}

static void replace_all_refs_to(const ir::schema& what, ir::schema_ref replace, ir::ir_object& obj) {
  assert(obj.schemas);
  for (auto& _s : *obj.schemas) {
    // меняем только то, что не влияет хеш и сравнения
    ir::schema& s = const_cast<ir::schema&>(_s);
    if (s.required) {
      for (auto& reqs : *s.required) {
        if (reqs.refers_to(what))
          reqs = replace;
      }
    }
    replace_in_type(what, replace, s.type);
  }
  if (obj.operations) {
    for (auto& _op : *obj.operations) {
      // меняем только то, что не влияет хеш и сравнения
      ir::operation& op = const_cast<ir::operation&>(_op);
      replace_in_operation(what, replace, op);
    }
  }
  if (obj.webhooks) {
    for (auto& _wh : *obj.webhooks) {
      // меняем только то, что не влияет хеш и сравнения
      ir::operation& op = const_cast<ir::operation&>(_wh);
      replace_in_operation(what, replace, op);
    }
  }
}

ir::ir_object pass_rm_trivial_schemas(ir::ir_object obj) {
  if (!obj.schemas)
    return obj;
  auto it = obj.schemas->begin();
  auto e = obj.schemas->end();
  // TODO может ли быть что не все за один пасс убрались (новые появились?)
  for (; it != e;) {
    auto& s = *it;
    if (s.is_trivial()) {
      ir::schema_ref ref = s.ref();
      replace_all_refs_to(s, ir::make_fundamental_ref(std::get<ir::fundamental_e>(s.type.data)), obj);
      it = obj.schemas->erase(it);
    } else {
      ++it;
    }
  }
  return obj;
}

}  // namespace svarg
