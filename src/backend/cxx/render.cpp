#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/namespace_strings.hpp"
#include "svarg/backend/cxx/render.hpp"
#include "svarg/errors.hpp"
#include "svarg/ir/ir_visitor.hpp"

#include "svarg/backend/cxx/support/types.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"

namespace svarg::cxx {

// используется для опциональных полей
static constexpr std::string_view optional_str = "std::optional";
static constexpr std::string_view variant_str = "std::variant";
// используется также как optional, но когда поля рекурсивные (т.е. нельзя сделать optional)
static constexpr std::string_view box_str = "metel::box";
// название шаблона куда подставлять аргументы allOf
static constexpr std::string_view allof_str = "allof";
static constexpr std::string_view anyof_str = "anyof";
static constexpr std::string_view oneof_str = "oneof";
static constexpr std::string_view array_str = "arrayof";
// куда вставлять аргумент 'not' из openapi
static constexpr std::string_view not_str = "not_a";
// для схемы без имени. Она будет в отдельном уникальном неймспейсе с этим именем
static constexpr std::string_view inline_schema_name = "schema";
// добавляется в начале каждой схемы
static constexpr std::string_view schema_file_begin = R"(
#pragma once

#include "common_types.hpp"

)";
// добавляется в конце каждой схемы
static constexpr std::string_view schema_file_end = "";
static constexpr std::string_view operation_file_begin = R"(
#pragma once

#include "common_types.hpp"

)";
// добавляется в конце каждой схемы
static constexpr std::string_view operation_file_end = "";
static constexpr std::string_view typelist_str = "typelist";

// используется как имя когда есть полутривиальные объекты с одним полем, которые не алиасы только потому
// что у них есть дополнительные требования или дискриминатор и тд
// например итоговая структура будет
// struct Name {
//    anyof<A, B, C> <special_field_name>; using
//    required_schemas = <typelist_str><...>:
// };
static constexpr std::string_view special_field_name = "data";

static std::string make_cxx_ident(std::string_view str, const render_options& options) {
  return options.simple_cxx_identifiers ? to_cxx_ident_simple(str) : to_cxx_ident(str);
}

static std::string make_namespace(std::string_view spec, const json_pointer& jp,
                                  const render_options& options) {
  std::string ns = options.simple_cxx_identifiers ? jsonptr2namespace_simple(jp) : jsonptr2namespace(jp);
  if (ns.empty())
    return ns;
  return std::format("{}::{}", make_cxx_ident(spec, options), ns);
}

static std::string make_namespace(std::string_view spec, const json_pointer& jp,
                                  std::string_view relative_spec, const json_pointer& relative,
                                  const render_options& options) {
  // обращение relative_spec + relative -> spec + jp
  return namespace_relative(make_namespace(relative_spec, relative, options),
                            make_namespace(spec, jp, options));
}

static std::string make_file_path(const ir::schema_ref& ref, const render_options& options) {
  // добавляем "/schemas" чтобы всё связанное с типами складывалось отдельно и не засоряло
  // папки operations/webhooks/директорию где это всё лежит. Все типы локализованы в schemas
  // при этом публичные в schemas/components/schemas
  std::string path = ref.spec + "/schemas";
  for (std::string_view seg : ref.jsonptr->segments()) {
    path += '/';
    path += make_cxx_ident(seg, options);
  }
  path += ".hpp";
  return path;
}

static std::string make_file_path(const ir::operation& ref, const render_options& options, bool is_webhook) {
  std::string path = ref.spec + (is_webhook ? "/webhooks" : "/operations");
  path += '/';
  path += ref.id;
  path += ".hpp";
  return path;
}

static std::string_view get_schema_name(const ir::schema& s, const render_options& options) {
  return s.name ? *s.name : inline_schema_name;
}
static std::string_view get_schema_name(const ir::schema_ref& s, const render_options& options) {
  return s.name ? *s.name : inline_schema_name;
}

static std::string render_string(std::string_view s) {
  // всегда raw строки с двойной кавычкой, чтобы избежать экранирования и тд
  return std::format("R\"\"({})\"\"", s);
}

static std::string render_parse_tag(cxx_support::parse_tag_e tag) {
  return std::format(
      "\nstatic constexpr cxx_support::parse_tag_e parse_tag = cxx_support::parse_tag_e::{};\n\n",
      e2str(tag));
}

static array<ir::object_field_info> as_object_fields(auto&& params) {
  array<ir::object_field_info> result;
  for (const ir::operation_parameter& p : params)
    result.push_back({.type = p.schema, .name = p.name, .required = p.required.value_or(false)});
  return result;
}

std::string renderer::render_schemas_array(const array<ir::schema_ref>& arr, std::string_view relspec,
                                           const json_pointer& relative) const {
  std::string result;
  if (arr.empty())
    return result;
  size_t i = 0;
  goto skip_first;
  for (; i < arr.size(); ++i) {
    result += ',';
  skip_first:
    result += render_schema_ref(arr[i], relspec, relative).typeref;
  }
  return result;
}

void renderer::collect_mentioned_first_layer(const ir::schema_ref& s, ir::schema_refs_t& out) const {
  if (s.is_fundamental())  // ссылка на фундаментал напрямую
    return;
  auto& t = schemaof(s);

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
        if (a.additional_properties)
          collect(*a.additional_properties);
        for (auto& f : a.fields)
          collect(f.type);
        return;
      },
  };
  std::visit(collector, t.type.data);
}

bool renderer::collect_all_mentioned(const ir::schema_ref& s, ir::schema_refs_t& out) const {
  collect_mentioned_first_layer(s, out);
  for (;;) {
    size_t size_before = out.size();
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

std::string renderer::render_discriminator(const ir::discriminator_desc& d, std::string_view relspec,
                                           const json_pointer& relative) const {
  std::string result = std::format("\nstatic constexpr inline std::string_view discriminator_field = {};\n\n",
                                   render_string(d.propertyName));
  result += "// `vtor` must accept type, e.g. []<typename X>() { ... }";
  result += "decltype(auto) discriminate(std::string_view str, auto&& vtor) {\n";
  for (auto&& [str, type] : d.mapping) {
    result += std::format("if (str == {}) {{ return vtor.template operator()<{}>(); }}\n", str,
                          render_schema_ref(type, relspec, relative).typeref);
  }
  result += "return vtor.template operator()<void>();\n";
  result += "}\n";
  return result;
}

schema_ref_result renderer::render_schema_ref(const ir::schema_ref& s, std::string_view relspec,
                                              const json_pointer& relative) const {
  if (s.is_fundamental())  // ссылка на фундаментал напрямую
    return render_fundamental_type(*s.if_fundamental());
  auto& t = schemaof(s);
  // если простой - пытаемся сгенерировать алиас
  // именованные даже простые алиасы оставляем, например чтобы oneof<A, B, C,...> не дублировать во многих
  // местах. Если пользователь в спеке дал ему имя - значит так и хотел
  if (!t.name && !t.has_requirements()) {
    if (auto* f = t.type.if_fundamental()) {
      // t.is_trivial() == true
      return render_fundamental_type(*f);
    }

    matcher vtor{
        [](const ir::fundamental_e&) -> std::string { std::unreachable(); },
        [&](const ir::array_schema& a) {
          return std::format("{}<{}>", array_str, render_schema_ref(a.element, relspec, relative).typeref);
        },
        [&](const ir::allof_schema& a) {
          return std::format("{}<{}>", allof_str, render_schemas_array(a.schemas, relspec, relative));
        },
        [&](const ir::anyof_schema& a) {
          if (a.discriminator)
            return std::string{};  // нетривиальный
          return std::format("{}<{}>", anyof_str, render_schemas_array(a.schemas, relspec, relative));
        },
        [&](const ir::oneof_schema& a) {
          if (a.discriminator)
            return std::string{};  // нетривиальный
          return std::format("{}<{}>", oneof_str, render_schemas_array(a.schemas, relspec, relative));
        },
        [&](const ir::not_schema& a) {
          return std::format("{}<{}>", not_str, render_schema_ref(a.type, relspec, relative).typeref);
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

  std::string ns = make_namespace(t.spec, t.jsonptr, relspec, relative, options);
  std::string_view name = get_schema_name(s, options);
  std::string result = ns.empty() ? std::string(name) : std::format("{}::{}", ns, name);
  return {.typeref = std::move(result), .is_alias = false};
}

bool renderer::is_recursive_field(const ir::schema& s, const ir::schema_ref& field_type) const {
  ir::schema_refs_t deps;
  return collect_all_mentioned(field_type, deps);
}

std::string renderer::render_optional_field(const ir::schema_ref& field, std::string_view name,
                                            std::string_view relspec, const json_pointer& relative,
                                            bool recursive) const {
  // oneof раскрывается в box_union, который в себе уже скрывает
  // опциональность требуемость поля узнаётся из is_required
  if (schemaof(field).type.is_oneof())
    return std::format("{} {}", render_schema_ref(field, relspec, relative).typeref,
                       make_cxx_ident(name, options));
  std::string_view tempstr = recursive ? box_str : optional_str;
  return std::format("{}<{}> {}", tempstr, render_schema_ref(field, relspec, relative).typeref,
                     make_cxx_ident(name, options));
}

std::string renderer::render_required_field(const ir::schema_ref& field, std::string_view name,
                                            std::string_view relspec, const json_pointer& relative) const {
  return std::format("{} {}", render_schema_ref(field, relspec, relative).typeref,
                     make_cxx_ident(name, options));
}

std::string renderer::render_object_field(const ir::object_field_info& field, std::string_view relspec,
                                          const json_pointer& relative, bool recursive_field) const {
  if (recursive_field && field.required) {
    throw render_error("field `{}` in type `{}#{}` recursive and required", field.name, relspec,
                       relative.escaped());
  }
  if (field.required)
    return render_required_field(field.type, field.name, relspec, relative);
  return render_optional_field(field.type, field.name, relspec, relative, recursive_field);
}

std::string renderer::render_object_fields(const array<ir::object_field_info>& fields,
                                           std::string_view relspec, const json_pointer& relative,
                                           std::optional<ir::schema_ref> fields_owner,
                                           std::optional<ir::schema_ref> additional_properties) const {
  auto is_recursive = [&](const ir::schema_ref& r) {
    return fields_owner ? is_recursive_field(schemaof(*fields_owner), r) : false;
  };

  std::string result;
  for (auto&& f : fields) {
    if (f.required) {
      result += render_object_field(f, relspec, relative, is_recursive(f.type));
      result += ";\n";
    }
  }
  // затем опциональные
  for (auto&& f : fields) {
    if (!f.required) {
      result += render_object_field(f, relspec, relative, is_recursive(f.type));
      result += ";\n";
    }
  }
  if (!additional_properties || additional_properties->is_fundamental(ir::fundamental_e::never)) {
    result += "\nusing additional_fields_type = void;\n\n";
  } else {
    result += std::format("\nusing additional_properties_type = {};\n\n",
                          render_schema_ref(*additional_properties, relspec, relative).typeref);
    // TODO обработать возможное пересечение имён с обычными полями
    result += "additional_properties_t<additional_properties_type> _additional_properties;\n\n";
  }
  result += "[[nodiscard]] static constexpr bool is_required(std::string_view str) noexcept {\n";
  for (auto&& f : fields) {
    if (f.required)
      result += std::format("if (str == {}) return true;\n", render_string(f.name));
  }
  result += "return false;\n";
  result += "}\n\n";
  return result;
}

// `typeref` - тип поля, если s полутривиальный объект
std::string renderer::render_struct_content(const ir::schema& s, std::string typeref) const {
  std::string field_str = std::format("{} {};", typeref, special_field_name);
  matcher vtor{
      [](const ir::fundamental_e&) -> std::string { std::unreachable(); },
      [&](const ir::array_schema& a) {
        return render_parse_tag(cxx_support::parse_tag_e::array) + std::move(field_str);
      },
      [&](const ir::allof_schema& a) {
        return render_parse_tag(cxx_support::parse_tag_e::allof) + std::move(field_str);
      },
      [&](const ir::anyof_schema& a) {
        std::string result = std::move(field_str);
        result += render_parse_tag(cxx_support::parse_tag_e::anyof);
        if (a.discriminator)
          result += render_discriminator(*a.discriminator, s.spec, s.jsonptr);
        return result;
      },
      [&](const ir::oneof_schema& a) {
        std::string result = std::move(field_str);
        result += render_parse_tag(cxx_support::parse_tag_e::oneof);
        if (a.discriminator)
          result += render_discriminator(*a.discriminator, s.spec, s.jsonptr);
        return result;
      },
      [&](const ir::not_schema& a) {
        return render_parse_tag(cxx_support::parse_tag_e::not_a) + std::move(field_str);
      },
      [&](const ir::enum_schema& e) -> std::string {
        bool is_string_enum = e.type.is_fundamental(ir::fundamental_e::string);
        std::string type = render_schema_ref(e.type, s.spec, s.jsonptr).typeref;
        std::string result;
        if (is_string_enum) {
          result += render_parse_tag(cxx_support::parse_tag_e::string_enum);
          result += std::format("static constexpr inline std::array<std::string_view, {}> values = {{",
                                e.values.size());
          for (auto&& v : e.values)
            result += render_string(std::string_view(v.value.as_string()));
          result += "};\n";  // end values
          result += "enum struct values_e {";
          for (auto&& v : e.values)
            result += std::format("{},", make_cxx_ident(std::string_view(v.value.as_string()), options));
          result += "};\nusing enum values_e;\n\n";  // end values_e
          result += std::format("values_e {};\n\n", special_field_name);
        } else {
          result += render_parse_tag(cxx_support::parse_tag_e::enum_);
          result += std::format("static const inline std::array<{}, {}> values = {{", type, e.values.size());
          if (e.type.is_fundamental(ir::fundamental_e::boolean)) {
            for (auto&& v : e.values)
              result += std::format("{},", from_json<cxx_support::boolean>(v.value));
          } else if (e.type.is_fundamental(ir::fundamental_e::integer)) {
            for (auto&& v : e.values)
              result += std::format("{},", from_json<cxx_support::integer>(v.value));
          } else if (e.type.is_fundamental(ir::fundamental_e::number)) {
            for (auto&& v : e.values)
              result += std::format("{},", from_json<cxx_support::number>(v.value).get_as_double());
          } else if (e.type.is_fundamental(ir::fundamental_e::null)) {
            for (auto&& v : e.values) {
              if (!v.value.is_null())
                throw parse_error("cannot parse `null` from not null json value `{}`",
                                  to_json_string(v, /*pretty=*/false));
              result += std::format("null,");
            }
          } else {
            throw render_error("not fundamental types in enums are not supported");
            // for (auto&& v : e.values)
            //   result += std::format("from_json_string<{}>({}),", type,
            //                         render_string(boost::json::serialize(v.value)));
          }
          result += "};\n";  // end values
          result += std::format("{} {};\n\n", type, special_field_name);
        }
        return result;
      },
      [&](const ir::object_schema& a) -> std::string {
        std::string result = render_parse_tag(cxx_support::parse_tag_e::object);
        result += render_object_fields(a.fields, s.spec, s.jsonptr, /*fields_owner=*/s.ref(),
                                       a.additional_properties);
        return result;
      },
  };
  std::string res = std::visit(vtor, s.type.data);
  if (s.has_requirements()) {
    res += std::format("using required_schemas = {}<{}>;\n", typelist_str,
                       render_schemas_array(*s.required, s.spec, s.jsonptr));
  }
  return res;
}

render_result renderer::render_schema(const ir::schema& s) const {
  std::string file_begin(schema_file_begin);
  const auto sref = s.ref();
  auto [typeref, is_alias] = render_schema_ref(sref, s.spec, s.jsonptr);
  if (is_alias)
    return {.filename = {}, .rendered = {}};
  {
    // include всех упомянутых в реализации зависимостей (рекурсия обрабатывается pragma once)
    ir::schema_refs_t deps;

    collect_mentioned_first_layer(sref, deps);
    deps = dealias(std::move(deps));
    for (auto& ref : deps)
      file_begin += std::format("#include \"{}\"\n", make_file_path(ref, options));
    file_begin += std::format("\nnamespace {} {{\n\n", make_namespace(s.spec, s.jsonptr, options));
  }
  std::string schema_render;

  if (is_alias) {
    schema_render = std::format("using {} = {};\n", get_schema_name(s, options), typeref);
  } else {
    schema_render = std::format("struct {} {{\n", get_schema_name(s, options));
    schema_render += render_struct_content(s, typeref);
    schema_render += "};\n\n";
  }

  file_begin += schema_render;
  file_begin += "\n}";  // конец неймспейса
  file_begin += schema_file_end;
  return {
      .filename = make_file_path(sref, options),
      .rendered = std::move(file_begin),
  };
}

std::vector<fs::path> renderer::render_all(fs::path output_dir, dd::generator<render_result> renders) const {
  std::error_code ec;
  // set для одинакового порядка
  std::set<fs::path> written;
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
  for (const auto& [filepath, rendered] : renders) {
    fs::path path = output_dir / filepath;
    auto [_, is_emplaced] = written.insert(path);
    if (!is_emplaced)
      throw render_error("two schemas rendered into same file `{}`", path.generic_string());
    fs::create_directories(path.parent_path(), ec);
    if (ec)
      throw render_error("filesystem error, message: {}, path: {}", ec.message(), path.generic_string());
    // заменяет файл если он уже есть
    write_into(path, rendered);
  }
  rm_written.no_longer_needed();
  return std::vector(written.begin(), written.end());
}

std::vector<fs::path> renderer::render_all_schemas(fs::path output_dir) const {
  auto foo = [&]() -> dd::generator<render_result> {
    for (auto&& s : schemas()) {
      auto rr = render_schema(s);
      if (!rr.filename.empty())  // алиас, файл не нужен
        co_yield std::move(rr);
    }
  };
  return render_all(std::move(output_dir), foo());
}

std::vector<fs::path> renderer::render_all_operations(fs::path output_dir, const std::set<ir::operation>& ops,
                                                      bool is_webhooks) const {
  auto foo = [&](this auto) -> dd::generator<render_result> {
    for (auto&& op : ops) {
      // workaround gcc ICE
      auto rr = render_operation(op, is_webhooks);
      co_yield std::move(rr);
    }
  };
  return render_all(std::move(output_dir), foo());
}

void renderer::render2cxx(fs::path output_dir) const {
  if (ir.schemas && !ir.schemas->empty())
    render_all_schemas(output_dir);
  if (ir.operations && !ir.operations->empty())
    render_all_operations(output_dir, *ir.operations, /*is_webhooks=*/false);
  if (ir.webhooks && !ir.webhooks->empty())
    render_all_operations(output_dir, *ir.webhooks, /*is_webhooks=*/true);

  // all_schemas.hpp / all_operations.hpp / all_webhooks.hpp для каждой спеки (даже если схем нет,
  // генерируется пустой файл)
  // #pragma once нет, так как это файлы состоящие из #include со своими pragma once

  // имя спеки -> содержимое файла со всеми схемами
  struct all_files_content {
    std::string all_schemas;
    std::string all_operations;
    std::string all_webhooks;
  };
  std::map<std::string, all_files_content> spec2rendered;
  if (ir.schemas) {
    for (auto& s : *ir.schemas) {
      // include только именованные, так как все неименованные (инлайн схемы) через них тоже подключатся, т.к.
      // их кто-то использует. Именованные схемы никогда не алиасы, то есть имеют свой файл
      if (s.name)
        spec2rendered[s.spec].all_schemas +=
            std::format("#include \"{}\"\n", make_file_path(s.ref(), options));
    }
  }
  if (ir.operations) {
    for (auto& op : *ir.operations) {
      spec2rendered[op.spec].all_operations +=
          std::format("#include \"{}\"\n", make_file_path(op, options, /*is_webhook=*/false));
    }
  }
  if (ir.webhooks) {
    for (auto& op : *ir.webhooks) {
      spec2rendered[op.spec].all_webhooks +=
          std::format("#include \"{}\"\n", make_file_path(op, options, /*is_webhook=*/true));
    }
  }
  for (auto& [spec, all] : spec2rendered) {
    write_into(output_dir / spec / "all_schemas.hpp", all.all_schemas);
    write_into(output_dir / spec / "all_operations.hpp", all.all_operations);
    write_into(output_dir / spec / "all_webhooks.hpp", all.all_webhooks);
  }
}

// ОПЕРАЦИИ

rendered_with_name renderer::render_body(const ir::body& body, std::string_view spec,
                                         const json_pointer& jp) const {
  std::string name = std::format("{}_body", make_cxx_ident(body.contentType, options));
  std::string result;
  result += std::format("struct {} {{\n", name);

  if (body.streaming && *body.streaming)
    result += "using is_streaming = int;\n";
  result += render_parse_tag(cxx_support::parse_tag_e::body);
  result += std::format("static constexpr std::string_view content_type = {};\n\n",
                        render_string(body.contentType));
  result += std::format("{} data;\n", render_schema_ref(body.schema, spec, jp).typeref);
  if (body.membersEncoding && !body.membersEncoding->empty()) {
    std::string encoding;
    for (auto& [prop_name, content_type] : *body.membersEncoding) {
      encoding +=
          std::format("if (str == {}) return {};\n", render_string(prop_name), render_string(content_type));
    }
    result += std::format(
        "[[nodiscard]] static constexpr std::string_view encoding_for(std::string_view str) noexcept"
        "{{ {} return \"\";\n}}\n\n",
        encoding);
  }

  result += "};\n";

  return {std::move(name), std::move(result)};
}

static std::string render_parameters(const auto& params, std::string_view relspec,
                                     const json_pointer& relative, const renderer& ren) {
  string s;

  s += ren.render_object_fields(as_object_fields(params), relspec, relative, /*fields_owner=*/std::nullopt,
                                /*additional_properties=*/std::nullopt);

  std::string encoding;
  for (const ir::operation_parameter& p : params) {
    if (p.encoding)
      encoding +=
          std::format("if (str == {}) return {};\n", render_string(p.name), render_string(*p.encoding));
  }
  if (!encoding.empty()) {
    s += std::format(
        "[[nodiscard]] static constexpr std::string_view encoding_for(std::string_view str) noexcept"
        "{{ {} return \"\";\n}}\n\n",
        encoding);
  }

  return s;
}

std::string renderer::render_bodies(const std::set<ir::body>& bodies, std::string_view spec,
                                    const json_pointer& jp) const {
  std::string result;
  if (bodies.empty())
    return result;
  if (bodies.size() > 1) {
    std::vector<std::string> struct_names;
    for (auto& b : bodies) {
      auto [struct_name, str] = render_body(b, spec, jp);
      struct_names.push_back(std::move(struct_name));
      result += str;
      result += '\n';
    }
    auto names_rng = struct_names | std::views::join_with(',');
    std::string types = std::format("{}", std::string(names_rng.begin(), names_rng.end()));
    result += std::format("{}<{}> body;\n\n", variant_str, types);
    // боди уже сортированы по типу контента так, что сначала провериться более специализированный
    result +=
        "static constexpr decltype(auto) select_body_type(std::string_view content_type, auto&& vtor) {";
    for (auto&& [b, struct_name] : std::views::zip(bodies, struct_names))
      result +=
          std::format("if (match_content_type({}, content_type)) return vtor.template operator()<{}>();\n",
                      render_string(b.contentType), struct_name);
    result += std::format("return vtor.template operator()<void>();\n");
    result += "}\n\n";
  } else {
    // одно тело
    auto [struct_name, str] = render_body(*bodies.begin(), spec, jp);
    result += str;
    result += std::format("\n{} body;\n", struct_name);
  }

  return result;
}

// если респонс не дефолтный генерирует
// static constexpr int status = ...;
// название структуры "response_{}" ("default_response" для дефолтного)
// генерирует все структуры боди и само боди (если одно - просто, если много - options.variant_str)
// при наличии хедеров структуру headers_t и поле headers
rendered_with_name renderer::render_response(const ir::response_desc& rsp, std::string_view spec,
                                             const json_pointer& jp) const {
  std::string result;
  std::string struct_name =
      rsp.is_default() ? "default_response" : std::format("response_{}", rsp.status.to_string());
  result += std::format("struct {} {{\n\n", struct_name);
  if (!rsp.is_default()) {
    if (!rsp.is_range())
      result += std::format("static constexpr int status = {};\n\n", rsp.status.to_string());
    else
      result += std::format("static constexpr status_range_e status_range = status_range_e::{};\n\n",
                            rsp.status.to_string());
  }
  if (rsp.headers) {
    std::vector<ir::operation_parameter> params;
    for (auto& h : *rsp.headers) {
      params.push_back(ir::operation_parameter{
          .location = parameter_location::header,
          .name = h.name,
          .schema = h.schema ? *h.schema : ir::make_fundamental_ref(ir::fundamental_e::string),
          .required = h.required,
      });
    }
    result += render_parameters(params, spec, jp, *this);
    result += '\n';
  }
  if (rsp.bodies)
    result += render_bodies(*rsp.bodies, spec, jp);
  result += "};\n\n";
  return {std::move(struct_name), std::move(result)};
}
/*
TODO:
* собрать что генерируется для схемы, операции
* генерализовать, упростить
* на основе генерируемого написать концепты, собрать требуемые типы
* на основе концептов написать вспомогательную библиотеку для подключения через смейк для сгенерированных
render2cxx таргетов
* написать смейк функцию генерации, которая генерирует таргеты, линкует вспомогательную библиотеку,
устанавливает нужные зависимости таргетам. И как-то сделать чтобы генерация была на этапе конфигурации, а не
на этапе билда
* написать дравйер запускающий фронтенд и бекенд, это другой способ использования - пользователь запускает
генерацию, получает необходимое, складывает куда ему нужно и при необходимости подключает вспомогательную
библиотеку (может быть даже вынесенную отдельно)


TODO пока использую воображаемый енам
enum struct parse_type_e {
path - именованный аргумент из path, нужно сложить все сегментированные имена тоже куда-то. Типа parse_path
какой-то если есть такие параметры
cookie, header, query,
// query_string
// + остальное body но там рантайм значение контент типа...
}

*/

static ir::schema_refs_t all_schemas_from(const auto& x, bool allow_fundamental = false) {
  ir::schema_refs_t schemas;
  auto foo = [&](const ir::schema_ref& s) {
    if (allow_fundamental || !s.is_fundamental())
      schemas.insert(s);
  };
  ir::schema_ref_vtor collector(foo);
  collector.visit(x);
  return schemas;
}

render_result renderer::render_operation(const ir::operation& op, bool is_webhook) const {
  std::string result(operation_file_begin);

  // обращения все из root, так как обращаемся из /paths в /components/schemas
  json_pointer paths("/paths");

  {  // includes
    auto used_schemas = dealias(all_schemas_from(op));
    for (auto& s : used_schemas)
      result += std::format("#include \"{}\"\n", make_file_path(s, options));
    result += '\n';
  }
  std::vector<const ir::operation_parameter*> path_params;
  std::vector<const ir::operation_parameter*> query_params;
  std::vector<const ir::operation_parameter*> header_params;
  std::vector<const ir::operation_parameter*> cookie_params;

  if (op.parameters) {
    for (auto& p : *op.parameters) {
      switch (p.location) {
        case parameter_location::path:
          path_params.push_back(&p);
          break;
        case parameter_location::query:
          query_params.push_back(&p);
          break;
        case parameter_location::header:
          header_params.push_back(&p);
          break;
        case parameter_location::cookie:
          cookie_params.push_back(&p);
          break;
      }
    }
  }

  result += std::format("namespace {} {{\n\n", make_namespace(op.spec, paths, options));

  // id
  result += std::format("struct {} {{\n", op.id);

  // spec
  result += std::format("static constexpr inline std::string_view spec = {};\n\n", render_string(op.spec));

  // method
  result += std::format("static constexpr inline std::string_view method = {};\n", render_string(op.method));

  // parameters

  auto deref = std::views::transform([](auto* p) -> auto& { return *p; });
  if (!path_params.empty()) {
    result += "struct path_t {\n";
    result += std::format("static constexpr std::string_view path_expr = {};\n\n", render_string(op.path));
    result += render_parameters(path_params | deref, op.spec, paths, *this);
    result += "};\n";
  }
  if (!query_params.empty()) {
    result += "struct query_t {\n";
    result += render_parameters(query_params | deref, op.spec, paths, *this);
    result += "};\n";
  }
  if (!header_params.empty()) {
    result += "struct headers_t {\n";
    result += render_parameters(header_params | deref, op.spec, paths, *this);
    result += "};\n";
  }
  if (!cookie_params.empty()) {
    result += "struct cookies_t {\n";
    result += render_parameters(cookie_params | deref, op.spec, paths, *this);
    result += "};\n";
  }
  result += '\n';
  if (!path_params.empty())
    result += "path_t path;\n";
  if (!query_params.empty())
    result += "query_t query;\n";
  if (!header_params.empty())
    result += "headers_t headers;\n";
  if (!cookie_params.empty())
    result += "cookies_t cookies;\n";

  // bodies

  if (op.bodies)
    result += render_bodies(*op.bodies, op.spec, paths);

  // responses/ defaultResponse

  // респонсов может быть не указано вообще - например в вебхуке для парсинга приходящих операций, а не
  // отправки
  if (op.defaultResponse)
    result += render_response(*op.defaultResponse, op.spec, paths).render;

  if (op.responses) {
    std::vector<std::string> struct_names;
    for (auto& r : *op.responses) {
      auto [struct_name, rendered] = render_response(r, op.spec, paths);
      result += rendered;
      struct_names.push_back(std::move(struct_name));
    }
    result += "constexpr decltype(auto) visit_response_status(int status, auto&& vtor) {\n";
    // сначала не ренжи (2ХХ)
    for (auto&& [struct_name, r] : std::views::zip(struct_names, *op.responses)) {
      assert(!r.is_default());
      if (!r.is_range()) {
        result += std::format("if (status == {}) return vtor.template operator()<{}>();\n",
                              r.status.to_string(), struct_name);
      }
    }
    // затам ренжи, чтобы более специализированный был проверен раньше
    for (auto&& [struct_name, r] : std::views::zip(struct_names, *op.responses)) {
      assert(!r.is_default());
      if (r.is_range()) {
        result += std::format(
            "if (match_status_range(status, status_range_e::{})) return vtor.template operator()<{}>();\n",
            r.status.to_string(), struct_name);
      }
    }
    if (op.defaultResponse)
      result += "return vtor.template operator()<default_response>();\n";
    else
      result += "return vtor.template operator()<void>();\n";
    result += "}\n\n";

    auto names_rng = struct_names | std::views::join_with(',');
    result += std::format("\nusing all_responses = {}<{}>;\n\n", typelist_str,
                          std::string(names_rng.begin(), names_rng.end()));
  }

  result += "};\n\n}\n";  // struct / namespace
  result += operation_file_end;
  return {make_file_path(op, options, is_webhook), std::move(result)};
}

ir::schema_refs_t renderer::dealias(ir::schema_refs_t schemas) const {
  auto is_alias = [this](const ir::schema_ref& s) { return render_schema_ref(s, {}, {}).is_alias; };
  ir::schema_refs_t removed_aliases;  // для обработки рекурсии в алиасах
  for (;;) {
    ir::schema_refs_t schemas2;
    bool has_aliases = false;
    for (auto& s : schemas) {
      if (removed_aliases.contains(s))
        continue;
      if (is_alias(s)) {
        has_aliases = true;
        removed_aliases.insert(s);
        ir::schema_refs_t deps;
        collect_mentioned_first_layer(s, deps);
        for (auto&& dep : deps)
          if (!removed_aliases.contains(dep))
            schemas2.insert(std::move(dep));
      } else {
        schemas2.insert(s);
      }
    }
    schemas = std::move(schemas2);
    if (!has_aliases)
      return schemas;
  }
}

}  // namespace svarg::cxx
