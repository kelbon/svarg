#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/namespace_strings.hpp"
#include "svarg/backend/cxx/render.hpp"

namespace svarg::cxx {

static std::string make_cxx_ident(std::string_view str, const render_options& options) {
  return options.simple_cxx_identifiers ? to_cxx_ident_simple(str) : to_cxx_ident(str);
}

static std::string make_namespace(const json_pointer& jp, const json_pointer& relative,
                                  const render_options& options) {
  return options.simple_cxx_identifiers ? namespace_relative_simple(relative, jp)
                                        : namespace_relative(relative, jp);
}

static std::string make_namespace(const json_pointer& jp, const render_options& options) {
  return options.simple_cxx_identifiers ? jsonptr2namespace_simple(jp) : jsonptr2namespace(jp);
}

static std::string make_file_path(const ir::schema_ref& ref, const render_options& options) {
  std::string path = ref.spec + "/schemas";
  for (std::string_view seg : ref.jsonptr->segments()) {
    path += '/';
    path += make_cxx_ident(seg, options);
  }
  return path;
}

static std::string render_schemas_array(const array<ir::schema_ref>& arr, const ir::schemas_t& schemas,
                                        const json_pointer& relative, const render_options& options) {
  std::string result;
  if (arr.empty())
    return result;
  size_t i = 0;
  goto skip_first;
  for (; i < arr.size(); ++i) {
    result += ',';
  skip_first:
    result += render_schema_ref(arr[i], schemas, relative, options).typeref;
  }
  return result;
}

static std::string_view get_schema_name(const ir::schema& s, const render_options& options) {
  return s.name ? *s.name : options.inline_schema_name;
}
static std::string_view get_schema_name(const ir::schema_ref& s, const render_options& options) {
  return s.name ? *s.name : options.inline_schema_name;
}

void collect_mentioned_first_layer(const ir::schema_ref& s, const ir::schemas_t& schemas,
                                   const render_options& options, ir::schema_refs_t& out) {
  if (s.is_fundamental())  // ссылка на фундаментал напрямую
    return;
  auto it = schemas.find(s);
  if (it == schemas.end())
    throw render_error("schema {}#{} do not refer to any schema in IR", s.spec, s.jsonptr->escaped());
  auto& t = *it;

  if (t.type.is_fundamental())
    return;

  auto collect = [&](const ir::schema_ref& ref) {
    if (ref != s && !ref.is_fundamental())  // рекурсия
      out.insert(ref);                      // если ref уже там был, то автоматически обрабатывается
  };
  auto collect_all = [&](const array<ir::schema_ref>& arr) {
    for (auto& ref : arr)
      collect(ref);
  };
  matcher collector{
      [](const ir::fundamental_e&) { std::unreachable(); },
      [&](const ir::array_schema& a) { return collect(a.element); },
      [&](const ir::allof_schema& a) { return collect_all(a.schemas); },
      [&](const ir::anyof_schema& a) { return collect_all(a.schemas); },
      [&](const ir::oneof_schema& a) { return collect_all(a.schemas); },
      [&](const ir::not_schema& a) { return collect(a.type); },
      [&](const ir::enum_schema& a) { return collect(a.type); },
      [&](const ir::object_schema& a) {
        if (a.additional_fields)
          collect(*a.additional_fields);
        for (auto& f : a.fields)
          collect(f.type);
        return;
      },
  };
  std::visit(collector, t.type.data);
}

bool collect_all_mentioned(const ir::schema_ref& s, const ir::schemas_t& schemas,
                           const render_options& options, ir::schema_refs_t& out) {
  for (;;) {
    size_t size_before = out.size();
    // TODO return true if recursive?
    for (auto& ref : out)
      collect_mentioned_first_layer(ref, schemas, options, out);
    if (size_before == out.size()) {
      // на случай если была рекурсия и сослались на самого себя
      return out.erase(s) > 0;
    }
  }
}

std::vector<std::string> collect_mentioned_first_layer_specs(const ir::schema_ref& s,
                                                             const ir::schemas_t& schemas,
                                                             const render_options& options) {
  ir::schema_refs_t out;
  collect_mentioned_first_layer(s, schemas, options, out);

  std::vector<std::string> specs;
  for (auto& r : out) {
    assert(!r.is_fundamental());
    if (!std::ranges::contains(specs, r.spec))
      specs.push_back(r.spec);
  }
  return specs;
}

schema_ref_result render_fundamental_type(const ir::fundamental_e f, const render_options&) {
  return {.typeref = std::string(e2str(f)), .is_alias = true};
}

std::string render_discriminator(const ir::discriminator_desc& d, const ir::schemas_t& schemas,
                                 const json_pointer& relative, const render_options& options) {
  std::string result = std::format("\nusing discriminator_field = {};\n\n", d.propertyName);
  result += "// `vtor` must accept type, e.g. []<typename X>() { ... }";
  result += "decltype(auto) discriminate(std::string_view str, auto&& vtor) {\n";
  for (auto&& [str, type] : d.mapping) {
    result += std::format("if (str == {}) {{ return vtor.template operator()<{}>(); }}", str,
                          render_schema_ref(type, schemas, relative, options).typeref);
  }
  result += "else { vtor.template operator()<void>(); }";
  result += '}';
  return result;
}

schema_ref_result render_schema_ref(const ir::schema_ref& s, const ir::schemas_t& schemas,
                                    const json_pointer& relative, const render_options& options) {
  if (s.is_fundamental())  // ссылка на фундаментал напрямую
    return render_fundamental_type(*s.if_fundamental(), options);
  auto it = schemas.find(s);
  if (it == schemas.end())
    throw render_error("schema {}#{} do not refer to any schema in IR", s.spec, s.jsonptr->escaped());
  auto& t = *it;
  // если простой - генерируем алиас
  if (!t.has_requirements()) {
    if (auto* f = t.type.if_fundamental()) {
      // t.is_trivial() == true
      return render_fundamental_type(*f, options);
    }

    matcher vtor{
        [](const ir::fundamental_e&) -> std::string { std::unreachable(); },
        [&](const ir::array_schema& a) {
          return std::format("{}<{}>", options.array_str,
                             render_schema_ref(a.element, schemas, relative, options).typeref);
        },
        [&](const ir::allof_schema& a) {
          return std::format("{}<{}>", options.allof_str,
                             render_schemas_array(a.schemas, schemas, relative, options));
        },
        [&](const ir::anyof_schema& a) {
          if (a.discriminator)
            return std::string{};  // нетривиальный
          return std::format("{}<{}>", options.anyof_str,
                             render_schemas_array(a.schemas, schemas, relative, options));
        },
        [&](const ir::oneof_schema& a) {
          // TODO какую-то функцию каждому типа to_alias или хз. Чтобы не потерять такие мелочи хз где
          if (a.discriminator)
            return std::string{};  // нетривиальный
          return std::format("{}<{}>", options.oneof_str,
                             render_schemas_array(a.schemas, schemas, relative, options));
        },
        [&](const ir::not_schema& a) {
          return std::format("{}<{}>", options.not_str,
                             render_schema_ref(a.type, schemas, relative, options).typeref);
        },
        [&](const ir::enum_schema&) -> std::string {
          // enum всегда преобразуется в структуру, внутри которой values и (для строк) enum + using enum
          return "";
        },
        [&](const ir::object_schema&) -> std::string {
          // объект всегда не алиас
          return "";
        },
    };
    std::string res = std::visit(vtor, t.type.data);
    if (!res.empty())
      return {.typeref = std::move(res), .is_alias = true};
  }
  // всё остальное на выводе это полноценные struct

  std::string ns = make_namespace(it->jsonptr, relative, options);
  std::string_view name = get_schema_name(s, options);
  std::string result = ns.empty() ? std::string(name) : std::format("{}::{}", ns, name);
  return {.typeref = std::move(result), .is_alias = false};
}

// возвращает true если тип field_type рекурсивно где-то содержит в себе `s`
static bool is_recursive_field(const ir::schema& s, const ir::schema_ref& field_type,
                               const ir::schemas_t& schemas, const render_options& options) {
  ir::schema_refs_t deps;
  collect_all_mentioned(field_type, schemas, options, deps);
  return deps.contains(s);  // рекурсия - поле зависит от самого типа
}

// TODO все методы в структуру cxx_renderer чтобы не прокидывать всё

std::string render_object_field(const ir::schema& s, const ir::object_field_info& field,
                                const ir::schemas_t& schemas, const json_pointer& relative,
                                const render_options& options) {
  bool recursive = is_recursive_field(s, field.type, schemas, options);
  if (recursive && field.required) {
    throw render_error("field `{}` in type `{}#{}` recursive and required", field.name, s.spec,
                       s.jsonptr.escaped());
  }
  if (field.required) {
    assert(!recursive);
    return std::format("{} {}", render_schema_ref(s.ref(), schemas, relative, options).typeref, field.name);
  }
  // optional
  std::string_view tempstr = recursive ? options.box_str : options.optional_str;
  return std::format("{}<{}> {}", tempstr, render_schema_ref(s.ref(), schemas, relative, options).typeref,
                     field.name);
}

// `typeref` - тип поля, если s полутривиальный объект
static std::string render_struct_content(const ir::schema& s, const ir::schemas_t& schemas,
                                         std::string typeref, const render_options& options) {
  std::string field_str = std::format("{} {};", typeref, options.special_field_name);
  matcher vtor{
      [](const ir::fundamental_e&) -> std::string { std::unreachable(); },
      [&](const ir::array_schema& a) {
        assert(s.has_requirements());
        return std::move(field_str);
      },
      [&](const ir::allof_schema& a) {
        assert(s.has_requirements());
        return std::move(field_str);
      },
      [&](const ir::anyof_schema& a) {
        // TODO? набор опшнлов? Потенциально с рекурсией... А точно, это должно быть внутри anyof...
        // потенциально можно генерировать специализации anyof для красоты
        // хотя можно геттеры сеттеры генерировать здесь
        std::string result = std::move(field_str);
        result += "\n\nusing inline_parse = int;\n";
        if (a.discriminator)
          result += render_discriminator(*a.discriminator, schemas, s.jsonptr, options);
        return result;
      },
      [&](const ir::oneof_schema& a) {
        std::string result = std::move(field_str);
        result += "\n\nusing inline_parse = int;\n";
        if (a.discriminator)
          result += render_discriminator(*a.discriminator, schemas, s.jsonptr, options);
        return result;
      },
      [&](const ir::not_schema& a) {
        assert(s.has_requirements());
        return std::move(field_str);
      },
      [&](const ir::enum_schema& e) -> std::string {
        std::string type = render_schema_ref(e.type, schemas, s.jsonptr, options).typeref;
        std::string result = std::format("{} {};\n\n", type, options.special_field_name);
        result += std::format("static std::array<{}, {}> values = {{", type, e.values.size());
        for (auto&& v : e.values) {
          if (e.type.is_fundamental(ir::fundamental_e::string))
            result += std::format("\"{}\",", std::string_view(v.value.as_string()));
          else
            result += std::format("from_json(\"{}\"),", boost::json::serialize(v.value));
        }
        result += "};\n";
        if (e.type.is_fundamental(ir::fundamental_e::string)) {
          result += "enum struct values_e {";
          for (auto&& v : e.values)
            result += std::format("{},", v.value.as_string());
          result += "};\nusing enum values_e;\n\n";
        }
        return result;
      },
      [&](const ir::object_schema& a) -> std::string {
        std::string result;
        for (auto&& f : a.fields) {
          result += render_object_field(s, f, schemas, s.jsonptr, options);
          result += ";\n";
        }
        if (!a.additional_fields) {
          result += "using additional_fields = void;\n\n";
        } else {
          result += std::format("using additional_fields = {};\n\n",
                                render_schema_ref(s.ref(), schemas, s.jsonptr, options).typeref);
        }
        result += "[[nodiscard]] static bool is_required_field(std::string_view str) noexcept {\n";
        for (auto&& f : a.fields) {
          if (f.required) {
            result += std::format("if (str == \"{}\") return true;\n", f.name);
            result += ";\n";
          }
        }
        result += "return false;\n";
        result += "}\n\n";
        // TODO геттеры или сеттеры
        return result;
      },
  };
  std::string res = std::visit(vtor, s.type.data);
  if (s.has_requirements()) {
    res += std::format("using required_schemas = {}<{}>", options.typelist_str,
                       render_schemas_array(*s.required, schemas, s.jsonptr, options));
  }
  return res;
}

render_schema_result render_schema(const ir::schema& s, const ir::schemas_t& schemas,
                                   const render_options& options) {
  std::string file_begin = options.schema_type_begin;
  const auto sref = s.ref();
  {
    // include всех упомянутых в реализации зависимостей (рекурсия обрабатывается pragma once)
    ir::schema_refs_t deps;

    collect_mentioned_first_layer(sref, schemas, options, deps);
    for (auto& ref : deps)
      file_begin += std::format("#include \"{}\"\n", make_file_path(sref, options));
    file_begin += std::format("\nnamespace {} {{\n", make_namespace(s.jsonptr, options));
  }
  std::string schema_render;

  auto [typeref, is_alias] = render_schema_ref(sref, schemas, s.jsonptr, options);
  if (is_alias) {
    schema_render = std::format("using {} = {};\n", get_schema_name(s, options), typeref);
  } else {
    schema_render = std::format("struct {} {{\n", get_schema_name(s, options));
    schema_render += render_struct_content(s, schemas, typeref, options);
    schema_render += "};\n\n";
  }

  file_begin += schema_render;
  file_begin += '}';  // конец неймспейса
  file_begin += options.schema_file_end;
  return {
      .filename = make_file_path(sref, options),
      .rendered_schema = std::move(file_begin),
  };
}

}  // namespace svarg::cxx
