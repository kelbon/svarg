#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/namespace_strings.hpp"
#include "svarg/backend/cxx/render.hpp"
#include "svarg/errors.hpp"

#include <fstream>

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
  return path + ".hpp";
}

static std::string_view get_schema_name(const ir::schema& s, const render_options& options) {
  return s.name ? *s.name : options.inline_schema_name;
}
static std::string_view get_schema_name(const ir::schema_ref& s, const render_options& options) {
  return s.name ? *s.name : options.inline_schema_name;
}

std::string renderer::render_schemas_array(const array<ir::schema_ref>& arr,
                                           const json_pointer& relative) const {
  std::string result;
  if (arr.empty())
    return result;
  size_t i = 0;
  goto skip_first;
  for (; i < arr.size(); ++i) {
    result += ',';
  skip_first:
    result += render_schema_ref(arr[i], relative).typeref;
  }
  return result;
}

void renderer::collect_mentioned_first_layer(const ir::schema_ref& s, ir::schema_refs_t& out) const {
  if (s.is_fundamental())  // ссылка на фундаментал напрямую
    return;
  auto it = schemas().find(s);
  if (it == schemas().end())
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

bool renderer::collect_all_mentioned(const ir::schema_ref& s, ir::schema_refs_t& out) const {
  for (;;) {
    size_t size_before = out.size();
    // TODO return true if recursive?
    for (auto& ref : out)
      collect_mentioned_first_layer(ref, out);
    if (size_before == out.size()) {
      // на случай если была рекурсия и сослались на самого себя
      return out.erase(s) > 0;
    }
  }
}

std::vector<std::string> renderer::collect_mentioned_first_layer_specs(const ir::schema_ref& s) const {
  ir::schema_refs_t out;
  collect_mentioned_first_layer(s, out);

  std::vector<std::string> specs;
  for (auto& r : out) {
    assert(!r.is_fundamental());
    if (!std::ranges::contains(specs, r.spec))
      specs.push_back(r.spec);
  }
  return specs;
}

schema_ref_result renderer::render_fundamental_type(const ir::fundamental_e f) const {
  return {.typeref = std::string(e2str(f)), .is_alias = true};
}

std::string renderer::render_discriminator(const ir::discriminator_desc& d,
                                           const json_pointer& relative) const {
  std::string result = std::format("\nusing discriminator_field = {};\n\n", d.propertyName);
  result += "// `vtor` must accept type, e.g. []<typename X>() { ... }";
  result += "decltype(auto) discriminate(std::string_view str, auto&& vtor) {\n";
  for (auto&& [str, type] : d.mapping) {
    result += std::format("if (str == {}) {{ return vtor.template operator()<{}>(); }}", str,
                          render_schema_ref(type, relative).typeref);
  }
  result += "else { vtor.template operator()<void>(); }";
  result += '}';
  return result;
}

schema_ref_result renderer::render_schema_ref(const ir::schema_ref& s, const json_pointer& relative) const {
  if (s.is_fundamental())  // ссылка на фундаментал напрямую
    return render_fundamental_type(*s.if_fundamental());
  auto it = schemas().find(s);
  if (it == schemas().end())
    throw render_error("schema {}#{} do not refer to any schema in IR", s.spec, s.jsonptr->escaped());
  auto& t = *it;
  // если простой - генерируем алиас
  if (!t.has_requirements()) {
    if (auto* f = t.type.if_fundamental()) {
      // t.is_trivial() == true
      return render_fundamental_type(*f);
    }

    matcher vtor{
        [](const ir::fundamental_e&) -> std::string { std::unreachable(); },
        [&](const ir::array_schema& a) {
          return std::format("{}<{}>", options.array_str, render_schema_ref(a.element, relative).typeref);
        },
        [&](const ir::allof_schema& a) {
          return std::format("{}<{}>", options.allof_str, render_schemas_array(a.schemas, relative));
        },
        [&](const ir::anyof_schema& a) {
          if (a.discriminator)
            return std::string{};  // нетривиальный
          return std::format("{}<{}>", options.anyof_str, render_schemas_array(a.schemas, relative));
        },
        [&](const ir::oneof_schema& a) {
          // TODO какую-то функцию каждому типа to_alias или хз. Чтобы не потерять такие мелочи хз где
          if (a.discriminator)
            return std::string{};  // нетривиальный
          return std::format("{}<{}>", options.oneof_str, render_schemas_array(a.schemas, relative));
        },
        [&](const ir::not_schema& a) {
          return std::format("{}<{}>", options.not_str, render_schema_ref(a.type, relative).typeref);
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
bool renderer::is_recursive_field(const ir::schema& s, const ir::schema_ref& field_type) const {
  ir::schema_refs_t deps;
  collect_all_mentioned(field_type, deps);
  return deps.contains(s);  // рекурсия - поле зависит от самого типа
}

// TODO все методы в структуру cxx_renderer чтобы не прокидывать всё

std::string renderer::render_object_field(const ir::schema& s, const ir::object_field_info& field,
                                          const json_pointer& relative) const {
  bool recursive = is_recursive_field(s, field.type);
  if (recursive && field.required) {
    throw render_error("field `{}` in type `{}#{}` recursive and required", field.name, s.spec,
                       s.jsonptr.escaped());
  }
  if (field.required) {
    assert(!recursive);
    return std::format("{} {}", render_schema_ref(field.type, relative).typeref, field.name);
  }
  // optional
  std::string_view tempstr = recursive ? options.box_str : options.optional_str;
  return std::format("{}<{}> {}", tempstr, render_schema_ref(field.type, relative).typeref, field.name);
}

// `typeref` - тип поля, если s полутривиальный объект
std::string renderer::render_struct_content(const ir::schema& s, std::string typeref) const {
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
          result += render_discriminator(*a.discriminator, s.jsonptr);
        return result;
      },
      [&](const ir::oneof_schema& a) {
        std::string result = std::move(field_str);
        result += "\n\nusing inline_parse = int;\n";
        if (a.discriminator)
          result += render_discriminator(*a.discriminator, s.jsonptr);
        return result;
      },
      [&](const ir::not_schema& a) {
        assert(s.has_requirements());
        return std::move(field_str);
      },
      [&](const ir::enum_schema& e) -> std::string {
        bool is_string_enum = e.type.is_fundamental(ir::fundamental_e::string);
        std::string type = render_schema_ref(e.type, s.jsonptr).typeref;
        std::string result = std::format("{} {};\n\n", type, options.special_field_name);
        if (is_string_enum)
          result += std::format("static constexpr inline std::array<std::string_view, {}> values = {{",
                                e.values.size());
        else
          result += std::format("static const inline std::array<{}, {}> values = {{", type, e.values.size());
        for (auto&& v : e.values) {
          if (is_string_enum)
            result += std::format("\"{}\",", std::string_view(v.value.as_string()));
          else
            result += std::format("from_json(\"{}\"),", boost::json::serialize(v.value));
        }
        result += "};\n";
        if (is_string_enum) {
          result += "enum struct values_e {";
          for (auto&& v : e.values)
            result += std::format("{},", make_cxx_ident(std::string_view(v.value.as_string()), options));
          result += "};\nusing enum values_e;\n\n";
        }
        return result;
      },
      [&](const ir::object_schema& a) -> std::string {
        std::string result;
        // сначала требуемые поля
        for (auto&& f : a.fields) {
          if (f.required) {
            result += render_object_field(s, f, s.jsonptr);
            result += ";\n";
          }
        }
        // затем опциональные
        for (auto&& f : a.fields) {
          if (!f.required) {
            result += render_object_field(s, f, s.jsonptr);
            result += ";\n";
          }
        }
        if (!a.additional_fields) {
          result += "\nusing additional_fields = void;\n\n";
        } else {
          result += std::format("\nusing additional_fields = {};\n\n",
                                render_schema_ref(s.ref(), s.jsonptr).typeref);
        }
        result += "[[nodiscard]] static constexpr bool is_required_field(std::string_view str) noexcept {\n";
        for (auto&& f : a.fields) {
          if (f.required)
            result += std::format("if (str == \"{}\") return true;\n", f.name);
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
                       render_schemas_array(*s.required, s.jsonptr));
  }
  return res;
}

render_schema_result renderer::render_schema(const ir::schema& s) const {
  std::string file_begin = options.schema_type_begin;
  const auto sref = s.ref();
  {
    // include всех упомянутых в реализации зависимостей (рекурсия обрабатывается pragma once)
    ir::schema_refs_t deps;

    collect_mentioned_first_layer(sref, deps);
    for (auto& ref : deps)
      file_begin += std::format("#include \"{}\"\n", make_file_path(sref, options));
    file_begin += std::format("\nnamespace {} {{\n\n", make_namespace(s.jsonptr, options));
  }
  std::string schema_render;

  auto [typeref, is_alias] = render_schema_ref(sref, s.jsonptr);
  if (is_alias) {
    schema_render = std::format("using {} = {};\n", get_schema_name(s, options), typeref);
  } else {
    schema_render = std::format("struct {} {{\n", get_schema_name(s, options));
    schema_render += render_struct_content(s, typeref);
    schema_render += "};\n\n";
  }

  file_begin += schema_render;
  file_begin += "\n}";  // конец неймспейса
  file_begin += options.schema_file_end;
  return {
      .filename = make_file_path(sref, options),
      .rendered_schema = std::move(file_begin),
  };
}

void renderer::render_all_schemas(fs::path output_dir) const {
  std::error_code ec;
  std::unordered_set<fs::path> written;
  on_scope_failure(rm_written) {
    std::error_code ec;
    // пытаемся не менять исходную папку, но всё же удаляем файлы, которые были перезаписаны
    for (auto& p : written) {
      fs::path dir = p.parent_path();
      fs::remove(p, ec);
      while (!ec && !dir.empty() && fs::is_empty(dir, ec))
        fs::remove(std::exchange(dir, dir.parent_path()), ec);
      if (ec)
        emit_warn("error while removing directory {}, err: {}", p.generic_string(), ec.message());
    }
  };
  for (const ir::schema& s : schemas()) {
    auto [filepath, rendered] = render_schema(s);
    fs::path path = output_dir / filepath;
    auto [_, is_emplaced] = written.insert(path);
    if (!is_emplaced)
      throw render_error("two schemas rendered into same file `{}`", path.generic_string());
    fs::create_directories(path.parent_path(), ec);
    if (ec)
      throw render_error("filesystem error, message: {}, path: {}", ec.message(), path.generic_string());
    // заменяет файл если он уже есть
    std::ofstream out(path);
    out << rendered;
    if (!out.is_open())
      throw render_error("cannot open file for writing, path: `{}`", path.generic_string());
  }
  rm_written.no_longer_needed();
}

void renderer::render2cxx(fs::path output_dir) const {
  if (ir.schemas)
    render_all_schemas(output_dir);
  // TODO operations etc
}

}  // namespace svarg::cxx
