#include "svarg/spec_loader.hpp"
#include "svarg/utils/matcher.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/parse_ref.hpp"

#include <boost/pfr.hpp>
#include <iostream>  // TODO rm
namespace svarg {

[[nodiscard]] ir::type_ref schema2type_ref(const schema_object& schema, const std::filesystem::path& file,
                                           std::string_view jsonptr);

// Вспомогательная: строит type_ref на inline-схему, добавляя её в контекст
// (предполагается что у тебя есть какой-то реестр типов — пока просто возвращаем type_ref с именем из
// jsonptr)
[[nodiscard]] ir::type_ref make_ref_for(std::string_view jsonptr) {
  auto pos = jsonptr.find_last_of('/');
  std::string_view name = jsonptr.substr(pos + 1);
  ir::type_ref ref;
  ref.scope = std::string(jsonptr.substr(0, pos + 1));  // всё кроме последнего сегмента
  if (!name.empty())
    ref.name = std::string(name);
  return ref;
}

[[nodiscard]] ir::type_ref schema_or_ref_to_type_ref(const reference_or<schema_object>& sor,
                                                     std::string_view base_jsonptr, size_t idx) {
  if (auto* ref = std::get_if<reference_object>(&sor)) {
    // $ref вида "#/components/schemas/Foo" — берём jsonptr после '#'
    auto& ref_str = ref->ref_;
    auto hash = ref_str.find('#');
    std::string jsonptr = (hash != std::string_view::npos) ? ref_str.substr(hash + 1) : ref_str;
    return make_ref_for(jsonptr);
  } else {
    // inline schema — синтетический jsonptr
    auto synthetic = std::format("{}/__inline_schema_{}", base_jsonptr, idx);
    // TODO: надо просто надеятся что кто-то собиравший типы проименовал правильно всё и мы в итоге укажем на
    // существующий тип
    return make_ref_for(synthetic);
  }
}

[[nodiscard]] ir::type frontend_schema2ir_type(const schema_object& schema, const std::filesystem::path& file,
                                               std::string_view jsonptr) {
  ir::type res;
  res.scope = jsonptr;

  if (!jsonptr.starts_with('/'))
    throw semanatic_error("invalid part of $ref after # (`{}`), must start with /", jsonptr);

  auto pos = jsonptr.find_last_of('/');
  std::string_view name = jsonptr.substr(pos + 1);
  if (!name.empty())
    res.name = std::string(name);

  // ── boolean schema ──────────────────────────────────────────────────────────
  if (schema.allow_always()) {
    res.data = ir::any_type{};
    return res;
  }
  if (schema.allow_never()) {
    res.data = ir::never_type{};
    return res;
  }

  // ── enum_ / const_ ─────────────────────────────────────────────────────────
  // const_ == enum с одним значением
  if (schema.const_) {
    res.data = ir::enum_type{.values = {*schema.const_}};
    return res;
  }
  if (schema.enum_) {
    if (schema.enum_->empty())
      throw semanatic_error("enum at `{}` must have at least one value", jsonptr);
    res.data = ir::enum_type{.values = *schema.enum_};
    return res;
  }

  // ── логические комбинаторы ──────────────────────────────────────────────────
  // Проверяем их до type, потому что type может отсутствовать при наличии anyOf и т.п.

  if (schema.allOf) {
    ir::allof_type t;
    for (size_t i = 0; i < schema.allOf->size(); ++i)
      t.types.push_back(schema_or_ref_to_type_ref((*schema.allOf)[i], jsonptr, i));
    res.data = std::move(t);
    return res;
  }

  if (schema.anyOf) {
    ir::anyof_type t;
    t.discriminator = schema.discriminator;
    for (size_t i = 0; i < schema.anyOf->size(); ++i)
      t.types.push_back(schema_or_ref_to_type_ref((*schema.anyOf)[i], jsonptr, i));
    res.data = std::move(t);
    return res;
  }

  if (schema.oneOf) {
    ir::oneof_type t;
    t.discriminator = schema.discriminator;
    for (size_t i = 0; i < schema.oneOf->size(); ++i)
      t.types.push_back(schema_or_ref_to_type_ref((*schema.oneOf)[i], jsonptr, i));
    res.data = std::move(t);
    return res;
  }

  if (schema.not_) {
    ir::not_type t;
    t.type = schema_or_ref_to_type_ref(*schema.not_, jsonptr, 0);
    res.data = std::move(t);
    return res;
  }

  // ── type: string | array<string> ───────────────────────────────────────────
  if (schema.type) {
    // Нормализуем к массиву
    array<string> types = schema.type->values;

    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());

    // nullable: добавляем "null" если ещё нет (OAS 3.0 совместимость)
    if (schema.nullable && *schema.nullable) {
      if (std::find(types.begin(), types.end(), "null") == types.end())
        types.push_back("null");
    }

    if (types.size() == 1) {
      // Единственный тип — прямо в fundamental или object_type
      const auto& t = types[0];

      if (t == "object") {
        ir::object_type ot;
        if (schema.properties) {
          size_t i = 0;
          for (auto& [prop_name, prop_sor] : *schema.properties) {
            on_scope_exit {
              ++i;
            };
            // synthetic jsonptr для свойства: jsonptr + "/properties/" + prop_name
            auto prop_ptr = std::format("{}/properties/{}", jsonptr, prop_name);
            ir::type_ref ref;
            if (std::holds_alternative<reference_object>(prop_sor)) {
              ref = schema_or_ref_to_type_ref(prop_sor, prop_ptr, i);
            } else {
              ref.name = prop_name;
              // scope — путь до properties/
              ref.scope = std::format("{}/properties/", jsonptr);
            }
            ot.fields.push_back(std::move(ref));
          }
        }
        if (schema.additionalProperties) {
          ot.additional_fields = schema_or_ref_to_type_ref(*schema.additionalProperties, jsonptr, 0);
        }
        res.data = std::move(ot);
        return res;
      }

      if (t == "array") {
        ir::array_type at;
        if (schema.items) {
          at.element = schema_or_ref_to_type_ref(*schema.items, jsonptr, 0);
        } else {
          throw parse_error("schema with type 'array' must contain 'items'");
        }
        res.data = std::move(at);
        return res;
      }

      // Всё остальное — фундаментальный тип
      ir::fundamental_e fe;
      efromstr(t, fe);
      res.data = fe;
      return res;
    }

    // Несколько типов → anyOf из фундаментальных type_ref-ов
    // Например: ["string", "null"] или ["integer", "string"]
    ir::anyof_type anyof;
    for (auto& t : types) {
      ir::fundamental_e fe;
      efromstr(t, fe);
      if (fe == ir::fundamental_e::object || fe == ir::fundamental_e::array) {
        throw semanatic_error(
            "field type with several types, where one of them object or array is not supported");
      }
      ir::type_ref ref;
      ref.fundamental = fe;
      anyof.types.push_back(std::move(ref));
    }
    res.data = std::move(anyof);
    return res;
  }

  // ── нет type, нет комбинаторов, но есть properties → object ───────────────
  if (schema.properties) {
    ir::object_type ot;
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      ir::type_ref ref;
      ref.name = prop_name;
      ref.scope = std::format("{}/properties/", jsonptr);
      ot.fields.push_back(std::move(ref));
    }
    if (schema.additionalProperties)
      ot.additional_fields = schema_or_ref_to_type_ref(*schema.additionalProperties, jsonptr, 0);
    res.data = std::move(ot);
    return res;
  }

  // ── полностью пустая схема без type → any ──────────────────────────────────
  res.data = ir::any_type{};
  return res;
}

// TODO эффективнее
static std::string scopes2jsonptr(std::span<const std::string_view> scopes) {
  std::string s;
  for (std::string_view str : scopes) {
    s += str;
    s.push_back('/');
  }
  if (s.ends_with('/') && s != "/")
    s.pop_back();
  return s;
}

// ожидает что file относительно spec dir
ir::type spec_loader::resolve_ref(reference_object ref, std::filesystem::path file) {
  parsed_ref r;
  for (size_t recursion_depth = 0; recursion_depth <= opts.ref_max_depth; ++recursion_depth) {
    r = parse_ref(ref.ref_);
    std::filesystem::path refered_file = file.parent_path() / r.file;
    const boost::json::value& v = jcache.load(refered_file);
    const boost::json::value& jref = v.at_pointer(r.json_ptr);
    auto ref_or_schema = parse_from_json<reference_or<schema_object>>(jref);
    if (auto* s = std::get_if<schema_object>(&ref_or_schema)) {
      return frontend_schema2ir_type(*s, std::filesystem::relative(refered_file, specs_dir), r.json_ptr);
    }
    ref = std::get<reference_object>(std::move(ref_or_schema));
    file = std::move(refered_file);
  }
  throw semanatic_error("recursion depth max reached");  // TODO мб собрать весь стек раскрутки ссылки
  // TODO нужно как то не грузить многократно одинаковые ссылки. Кешировать мб файл + json ptr -> type
}

struct vtor {
  std::filesystem::path obj_file;
  spec_loader* loader;
  std::vector<ir::type> types;
  std::vector<std::string_view> scopes;

  template <typename T>
  void operator()(std::string_view, const array<T>& f) {
  }
  template <typename T>
  void operator()(std::string_view, const string_map<T>& f) {
  }
  void operator()(std::string_view, const uint64_t& f) {
  }
  void operator()(std::string_view, const int64_t& f) {
  }
  void operator()(std::string_view, const number& f) {
  }
  void operator()(std::string_view, const bool& f) {
  }
  void operator()(std::string_view, const Any& f) {
  }
  void operator()(std::string_view, const string& f) {
  }
  void operator()(std::string_view, const parameter_location& f) {
  }
  void operator()(std::string_view name, const aggregate auto& x) {
    scopes.push_back(name);
    std::cout << "->" << name << std::endl;
    on_scope_exit {
      scopes.pop_back();
      std::cout << "<-" << name << std::endl;
    };
    boost::pfr::for_each_field_with_name(x, *this);
  }

  void operator()(std::string_view, const schema_object& x) {
    // TODO O(1)
    ir::type t = frontend_schema2ir_type(x, obj_file, scopes2jsonptr(scopes));
    if (!std::ranges::contains(types, t))
      types.push_back(std::move(t));
  }

  void operator()(std::string_view name, const reference_or<schema_object>& x) {
    std::visit([&](auto& x) { (*this)(name, x); }, x);
  }
  void operator()(std::string_view, const reference_object& x) {
    ir::type t = loader->resolve_ref(x, obj_file);
    // TODO O(1)
    if (!std::ranges::contains(types, t))
      types.push_back(std::move(t));
  }
  template <typename T>
  void operator()(std::string_view name, const optional<T>& x) {
    if (x)
      (*this)(name, *x);
  }
};

ir::ir_object spec_loader::visit_and_generate_ir(const openapi_object& obj,
                                                 const std::filesystem::path& obj_file) {
  vtor v(obj_file);
  boost::pfr::for_each_field_with_name(obj, v);

  return ir::ir_object{.types = std::move(v.types)};
}

ir::ir_object spec_loader::generate() {
  ir::ir_object res;
  for (auto& p : loaded_specs) {
    assert(specs.contains(p));
    auto ir = visit_and_generate_ir(specs.at(p), p);
    res.merge(std::move(ir));
  }
  return res;
}

}  // namespace svarg
