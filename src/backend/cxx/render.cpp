#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/namespace_strings.hpp"
#include "svarg/backend/cxx/render.hpp"
#include "svarg/errors.hpp"
#include "svarg/ir/ir_visitor.hpp"

#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/utils/parse_url.hpp"

#include <limits>

namespace svarg::cxx {

using cxx_support::special_field_name;

// используется для опциональных полей
static constexpr std::string_view optional_str = "std::optional";
static constexpr std::string_view variant_str = "std::variant";
// используется также как optional, но когда поля рекурсивные (т.е. нельзя сделать optional)
static constexpr std::string_view box_str = "::metel::box";
// название шаблона куда подставлять аргументы allOf
static constexpr std::string_view allof_str = "::svarg::cxx_support::allof";
static constexpr std::string_view anyof_str = "::svarg::cxx_support::anyof";
static constexpr std::string_view oneof_str = "::svarg::cxx_support::oneof";
static constexpr std::string_view array_str = "::svarg::cxx_support::arrayof";
// куда вставлять аргумент 'not' из openapi
static constexpr std::string_view not_str = "::svarg::cxx_support::not_a";
// для схемы без имени. Она будет в отдельном уникальном неймспейсе с этим именем
static constexpr std::string_view inline_schema_name = "_schema";
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
static constexpr std::string_view typelist_str = "::metel::typelist";

// выключено, чтобы генерировать любые апи. Ошибка будет выдана на этапе попытки записи в такой формат
// static void throw_if_unsupported_content_type(const ir::content_type_t& t) {
//   cxx_support::content_type_e e;
//   if (!efromstr(t.str(), e))
//     throw render_error("content type `{}` unsupported", t.str());
// }

static std::string make_cxx_ident(std::string_view str, const render_options& options) {
  return options.simple_cxx_identifiers ? to_cxx_ident_simple(str) : to_cxx_ident(str);
}

static std::string make_namespace(std::string_view spec, json_pointer jp, const render_options& options) {
  if (jp.segments_count() > 0)
    jp.rm_last_segment();
  std::string ns = options.simple_cxx_identifiers ? jsonptr2namespace_simple(jp) : jsonptr2namespace(jp);
  if (ns.empty())
    return make_cxx_ident(spec, options);
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

namespace detail {

inline std::string escape_plain(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        // управляющие символы и прочее странное пишется числовым кодом
        if (c < 0x20 || c == 0x7F) {
          // ровно 3 восьмеричных разряда — escape не "съест" следующий символ
          out += std::format("\\{:03o}", c);
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

}  // namespace detail

static std::string render_string(std::string_view s) {
  return std::format("\"{}\"", detail::escape_plain(s));
}

static std::string render_parse_tag(cxx_support::parse_tag_e tag) {
  return std::format(
      "\nstatic constexpr svarg::cxx_support::parse_tag_e parse_tag = "
      "svarg::cxx_support::parse_tag_e::{};\n\n",
      e2str(tag));
}

// граница для cxx_support::bound_t
static std::string render_bound(const number& n) {
  return std::visit(
      []<typename T>(T v) -> std::string {
        if constexpr (std::is_same_v<T, int64_t>) {
          if (v == std::numeric_limits<int64_t>::min())
            return "::svarg::cxx_support::bound_t(std::numeric_limits<int64_t>::min())";
          return std::format("::svarg::cxx_support::bound_t(int64_t({}))", v);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          return std::format("::svarg::cxx_support::bound_t(uint64_t({}u))", v);
        } else {
          // {} даёт кратчайшее представление, которое читается обратно в то же значение
          return std::format("::svarg::cxx_support::bound_t(double({}))", v);
        }
      },
      n.data);
}

// ограничения рендерятся в тип-владелец, а не отдельными типами
// поля в порядке объявления cxx_support::constraints_t (designated initializers)
static std::string render_constraints(const std::vector<const ir::constraints_schema*>& cs) {
  std::string result = std::format(
      "static constexpr std::array<::svarg::cxx_support::constraints_t, {}> constraints = {{{{\n", cs.size());
  for (const ir::constraints_schema* c : cs) {
    std::string fields;
    auto add = [&](std::string_view name, std::string value) {
      fields += std::format(".{} = {},\n", name, value);
    };
    if (c->minLength)
      add("min_length", std::format("{}u", *c->minLength));
    if (c->maxLength)
      add("max_length", std::format("{}u", *c->maxLength));
    if (c->pattern)
      add("pattern", render_string(*c->pattern));
    if (c->minimum)
      add("minimum", render_bound(*c->minimum));
    if (c->maximum)
      add("maximum", render_bound(*c->maximum));
    if (c->exclusiveMinimum)
      add("exclusive_minimum", render_bound(*c->exclusiveMinimum));
    if (c->exclusiveMaximum)
      add("exclusive_maximum", render_bound(*c->exclusiveMaximum));
    if (c->multipleOf)
      add("multiple_of", render_bound(*c->multipleOf));
    if (c->minItems)
      add("min_items", std::format("{}u", *c->minItems));
    if (c->maxItems)
      add("max_items", std::format("{}u", *c->maxItems));
    if (c->uniqueItems && *c->uniqueItems)
      add("unique_items", "true");
    if (c->minProperties)
      add("min_properties", std::format("{}u", *c->minProperties));
    if (c->maxProperties)
      add("max_properties", std::format("{}u", *c->maxProperties));
    result += std::format("::svarg::cxx_support::constraints_t{{\n{}}},\n", fields);
  }
  result += "}};\n";
  return result;
}

static arrayof<ir::object_field_info> as_object_fields(auto&& params) {
  arrayof<ir::object_field_info> result;
  for (const ir::operation_parameter& p : params)
    result.push_back({.type = p.schema, .name = p.name, .required = p.required.value_or(false)});
  return result;
}

std::string renderer::render_schemas_array(const arrayof<ir::schema_ref>& arr, std::string_view relspec,
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
  auto collect_all = [&](const arrayof<ir::schema_ref>& arr) {
    for (auto& ref : arr)
      collect(ref);
  };
  matcher collector{
      [](const ir::fundamental_e&) { metel::unreachable(); },
      [&](const ir::array_schema& a) { return collect(a.element); },
      [&](const ir::allof_schema& a) { return collect_all(a.schemas); },
      [&](const ir::anyof_schema& a) { return collect_all(a.schemas); },
      [&](const ir::oneof_schema& a) { return collect_all(a.schemas); },
      [&](const ir::not_schema& a) { return collect(a.type); },
      [&](const ir::enum_schema& a) { return collect(a.type); },
      // ограничения рендерятся внутрь владельца и ни на что не ссылаются
      [&](const ir::constraints_schema&) {},
      [&](const ir::object_schema& a) {
        collect(a.additional_properties);
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
  // полностью квалифицированные, генерируемый код находится в неймспейсе спеки
  if (f == ir::fundamental_e::string)
    return {.typeref = "std::string", .is_alias = true};
  return {.typeref = std::format("::svarg::cxx_support::{}", e2str(f)), .is_alias = true};
}

std::string renderer::render_discriminator(const ir::discriminator_desc& d, std::string_view relspec,
                                           const json_pointer& relative) const {
  // discriminator_field
  std::string result = std::format("\nstatic constexpr inline std::string_view discriminator_field = {};\n\n",
                                   render_string(d.propertyName));
  result += "// `vtor` must accept type, e.g. []<typename X>() { ... }";
  // discriminate
  result += "static decltype(auto) discriminate(std::string_view str, auto&& vtor) {\n";
  for (auto&& [str, type] : d.mapping) {
    result += std::format("if (str == {}) {{ return vtor.template operator()<{}>(); }}\n", render_string(str),
                          render_schema_ref(type, relspec, relative).typeref);
  }
  result += "return vtor.template operator()<void>();\n";
  result += "}\n";

  // discriminator_for
  result += "template<typename T> static std::string_view discriminator_for() {\n";
  for (auto&& [str, type] : d.mapping) {
    result += std::format("if constexpr (std::is_same_v<T, {}>) {{ return std::string_view({}); }}\n",
                          render_schema_ref(type, relspec, relative).typeref, render_string(str));
  }
  result += "return std::string_view{};\n";
  result += "}\n";

  return result;
}

std::string renderer::render_special_field_type(const ir::schema_type& type, std::string_view relspec,
                                                const json_pointer& relative) const {
  matcher vtor{
      [&](const ir::fundamental_e& e) -> std::string { return render_fundamental_type(e).typeref; },
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
      [&](const ir::constraints_schema&) -> std::string {
        // рендерится внутрь владельца (см. render_struct_content)
        return "";
      },
  };
  return std::visit(vtor, type.data);
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
    std::string res = render_special_field_type(t.type, relspec, relative);
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
  if (!field.is_fundamental() && schemaof(field).type.is_oneof())
    return std::format("{} {}", render_schema_ref(field, relspec, relative).typeref,
                       make_cxx_ident(name, options));
  std::string_view tempstr = recursive ? box_str : optional_str;
  return std::format("{}<{}> {}", tempstr, render_schema_ref(field, relspec, relative).typeref,
                     make_cxx_ident(name, options));
}

std::string renderer::render_required_field(const ir::schema_ref& field, std::string_view name,
                                            std::string_view relspec, const json_pointer& relative,
                                            bool recursive) const {
  if (recursive) {
    return std::format("{}<{}> {}", box_str, render_schema_ref(field, relspec, relative).typeref,
                       make_cxx_ident(name, options));
  }
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
    return render_required_field(field.type, field.name, relspec, relative, recursive_field);
  return render_optional_field(field.type, field.name, relspec, relative, recursive_field);
}

std::string renderer::render_object_fields(const arrayof<ir::object_field_info>& fields,
                                           std::string_view relspec, const json_pointer& relative,
                                           std::optional<ir::schema_ref> fields_owner,
                                           std::optional<ir::schema_ref> additional_properties) const {
  auto is_recursive = [&](const ir::schema_ref& r) {
    if (r.is_fundamental())
      return false;
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
    result += std::format("\nusing {} = void;\n\n", cxx_support::additional_properties_schema_alias_name);
  } else {
    result += std::format("\nusing {} = {};\n\n", cxx_support::additional_properties_schema_alias_name,
                          render_schema_ref(*additional_properties, relspec, relative).typeref);
    // TODO обработать возможное пересечение имён с обычными полями
    result += std::format("additional_properties_t<{}> {};\n\n",
                          cxx_support::additional_properties_schema_alias_name,
                          cxx_support::additional_properties_field_name);
  }
  result += "[[nodiscard]] static constexpr bool is_required(std::string_view str) noexcept {\n";
  for (auto&& f : fields) {
    if (f.required)  // оперирует именами из спецификации, а не из полей
      result += std::format("if (str == {}) return true;\n", render_string(f.name));
  }
  result += "return false;\n";
  result += "}\n\n";
  // имена полей нельзя использовать, так как С++ имена могут не совпадать с указанными в спецификации
  // порядок такой же как в декларациях - сначала требуемые, затем опциональные
  result +=
      std::format("static constexpr std::array<std::string_view, {}> field_names = {{\n", fields.size());
  for (auto&& f : fields) {
    if (f.required)
      result += std::format("{},\n", render_string(f.name));
  }
  for (auto&& f : fields) {
    if (!f.required)
      result += std::format("{},\n", render_string(f.name));
  }
  result += "};\n\n";
  return result;
}

// `typeref` - тип поля, если s полутривиальный объект
std::string renderer::render_struct_content(const ir::schema& s, std::string typeref) const {
  auto make_field_str = [](std::string_view fieldtype) {
    return std::format("{} {};", fieldtype, special_field_name);
  };
  auto render_requirements = [&]() -> std::string {
    if (!s.has_requirements())
      return "";
    // ограничения проверяются по разобранному значению и рендерятся прямо во владельца,
    // остальные требования - списком типов, по которым значение разбирается повторно
    std::string res;
    arrayof<ir::schema_ref> parsed_requirements;
    std::vector<const ir::constraints_schema*> constraints;
    for (const ir::schema_ref& r : *s.required) {
      if (!r.is_fundamental() && schemaof(r).type.is_constraints())
        constraints.push_back(schemaof(r).type.if_constraints());
      else
        parsed_requirements.push_back(r);
    }
    if (!parsed_requirements.empty()) {
      res += std::format("using required_schemas = {}<{}>;\n", typelist_str,
                         render_schemas_array(parsed_requirements, s.spec, s.jsonptr));
    }
    if (!constraints.empty())
      res += render_constraints(constraints);
    return res;
  };
  // пытаемся алиас, для объектов и енамов возвращает пустую строку
  std::string r = render_special_field_type(s.type, s.spec, s.jsonptr);
  if (!r.empty()) {
    // тип мог бы быть алиасом, но у него есть имя или требования, поэтому это структура с одним полем
    using enum cxx_support::parse_tag_e;
    cxx_support::parse_tag_e tag = std::visit(
        matcher_r<cxx_support::parse_tag_e>(
            [](const ir::fundamental_e&) { return fundamental; },
            [](const ir::array_schema&) { return array; }, [](const ir::allof_schema&) { return allof; },
            [](const ir::anyof_schema&) { return anyof; }, [](const ir::oneof_schema&) { return oneof; },
            [](const ir::not_schema&) { return not_a; },
            [](const auto&) -> cxx_support::parse_tag_e { metel::unreachable(); }),
        s.type.data);
    return render_parse_tag(tag) + std::format("{} {};\n", r, special_field_name) + render_requirements();
  }
  matcher vtor{
      [&](const ir::fundamental_e& f) -> std::string { metel::unreachable(); },
      [&](const ir::array_schema& a) -> std::string { metel::unreachable(); },
      [&](const ir::allof_schema& a) -> std::string { metel::unreachable(); },
      [&](const ir::anyof_schema& a) -> std::string { metel::unreachable(); },
      [&](const ir::oneof_schema& a) -> std::string { metel::unreachable(); },
      [&](const ir::not_schema& a) -> std::string { metel::unreachable(); },
      [&](const ir::constraints_schema&) -> std::string {
        // встречается только как требование и рендерится внутрь владельца, своего типа нет
        throw render_error("constraints schema {}#{} is used not as requirement", s.spec,
                           s.jsonptr.escaped());
      },
      [&](const ir::enum_schema& e) -> std::string {
        bool is_string_enum = e.type.is_fundamental(ir::fundamental_e::string);
        std::string type = render_schema_ref(e.type, s.spec, s.jsonptr).typeref;
        std::string result;
        if (is_string_enum) {
          result += render_parse_tag(cxx_support::parse_tag_e::string_enum);
          result += std::format("static constexpr inline std::array<std::string_view, {}> values = {{",
                                e.values.size());
          /*
          сериализация вместо .as_string обходит такое:

          type:string
          enum:
            - null
            или
            - 1
            - 2

            или даже то, что с точки зрения json похоже на объекты и массивы
            Т.е. по сути сериализация вместо .as_string убирает требование кавычек вокруг значений
          */
          for (auto&& v : e.values)
            result += std::format("{}, ", render_string(std::string_view(boost::json::serialize(v.value))));
          result += "};\n";  // end values
          result += "enum struct values_e {";
          for (auto&& v : e.values) {
            // боюсь представить идентификатор созданный из какого-то объекта, но ты сам виноват если такое
            // делаешь
            result += std::format("{},",
                                  make_cxx_ident(std::string_view(boost::json::serialize(v.value)), options));
          }
          result += "};\nusing enum values_e;\n\n";  // end values_e
          // возможное пересечение имён using enum / special_field_name игнорируется
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
              result += "::svarg::cxx_support::null{},";
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
  return std::visit(vtor, s.type.data) + render_requirements();
}

render_result renderer::render_schema(const ir::schema& s) const {
  std::string file_begin(schema_file_begin);
  // ограничения рендерятся внутрь владельца, своего файла нет
  if (s.type.is_constraints())
    return {.filename = {}, .rendered = {}};
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
    write_file(path, rendered);
  }
  rm_written.no_longer_needed();
  return std::vector(written.begin(), written.end());
}

std::vector<fs::path> renderer::render_all_schemas(fs::path output_dir) const {
  auto foo = [r = this](this auto) -> dd::generator<render_result> {
    for (auto&& s : r->schemas()) {
      auto rr = r->render_schema(s);
      if (!rr.filename.empty())  // алиас, файл не нужен
        co_yield std::move(rr);
    }
  };
  if (!ir.schemas)
    return {};
  return render_all(std::move(output_dir), foo());
}

std::vector<fs::path> renderer::render_all_operations(fs::path output_dir, const std::set<ir::operation>& ops,
                                                      bool is_webhooks) const {
  auto foo = [&, r = this](this auto) -> dd::generator<render_result> {
    for (auto&& op : ops) {
      // workaround gcc ICE
      auto rr = r->render_operation(op, is_webhooks);
      co_yield std::move(rr);
    }
  };
  return render_all(std::move(output_dir), foo());
}

std::vector<fs::path> renderer::render2cxx(fs::path output_dir) const {
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
    write_file(output_dir / spec / "all_schemas.hpp", all.all_schemas);
    write_file(output_dir / spec / "all_operations.hpp", all.all_operations);
    write_file(output_dir / spec / "all_webhooks.hpp", all.all_webhooks);
  }

  // server.hpp только если есть операции, один сервер на спеку (адрес сервера определяется listen)
  if (ir.operations) {
    string_map<std::vector<const ir::operation*>> spec2ops;
    for (auto& op : *ir.operations) {
      assert(op.has_path() || !op.servers);  // у вебхуков не должно быть серверов
      if (op.servers)
        spec2ops[op.spec].push_back(&op);
    }
    for (auto& [spec, ops] : spec2ops)
      write_file(output_dir / spec / "server.hpp", render_server(ops, spec));
  }

  if (ir.operations) {
    // по одному клиенту на каждую спеку

    // spec -> all operations
    string_map<std::vector<const ir::operation*>> clients;
    for (auto& op : *ir.operations) {
      assert(op.has_path() || !op.servers);  // у вебхуков не должно быть серверов
      if (op.servers) {
        for (auto& s : *op.servers)
          clients[op.spec].push_back(&op);
      }
    }
    // file -> rendered
    string_map<std::string> client_files;

    for (auto& [spec, ops] : clients)
      client_files[spec] = render_client(ops, spec);

    for (auto& [spec, rendered] : client_files)
      write_file(output_dir / spec / "client.hpp", rendered);
  }
  remove_duplicates(generated);
  std::stable_sort(generated.begin(), generated.end(),
                   [](const fs::path& a, const fs::path& b) { return a.extension() < b.extension(); });
  return std::move(generated);
}

// ОПЕРАЦИИ

rendered_with_name renderer::render_body(const ir::body& body, std::string_view spec,
                                         const json_pointer& jp) const {
  // throw_if_unsupported_content_type(body.contentType);
  std::string name = std::format("{}_body", make_cxx_ident(body.contentType, options));
  std::string result;
  result += std::format("struct {} {{\n", name);
  result += std::format("static constexpr bool is_required = {};\n", body.required);
  if (body.streaming && *body.streaming)
    result += "using is_streaming = int;\n";
  result += render_parse_tag(cxx_support::parse_tag_e::body);
  assert(is_lowered(body.contentType.str()));
  result += std::format("static constexpr std::string_view content_type = {};\n\n",
                        render_string(body.contentType));
  result += std::format("{} {};\n", render_schema_ref(body.schema, spec, jp).typeref, special_field_name);
  if (body.membersEncoding && !body.membersEncoding->empty()) {
    std::string encoding;
    for (auto& [prop_name, content_type] : *body.membersEncoding) {
      // throw_if_unsupported_content_type(content_type);
      encoding += std::format("if (str == {}) return {};\n",
                              render_string(make_cxx_ident(prop_name, options)), render_string(content_type));
    }
    result += std::format(
        "[[nodiscard]] static constexpr std::string_view encoding_for(std::string_view str) noexcept"
        "{{ {} return \"\";\n}}\n\n",
        encoding);
  }

  result += "};\n";

  return {std::move(name), std::move(result)};
}

std::string render_parameters(const auto& params, std::string_view relspec, const json_pointer& relative,
                              const renderer& ren) {
  string s;

  s += ren.render_object_fields(as_object_fields(params), relspec, relative, /*fields_owner=*/std::nullopt,
                                /*additional_properties=*/std::nullopt);

  std::string encoding;
  for (const ir::operation_parameter& p : params) {
    if (p.encoding) {
      // throw_if_unsupported_content_type(*p.encoding);
      encoding += std::format("if (str == {}) return {};\n",
                              render_string(make_cxx_ident(p.name, ren.options)), render_string(*p.encoding));
    }
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
    result += std::format("{}<std::monostate, {}> body;\n\n", variant_str, types);
    // боди уже сортированы по типу контента так, что сначала провериться более специализированный
    result +=
        "static constexpr decltype(auto) select_body_type(std::string_view content_type, auto&& vtor) {";
    for (auto&& [b, struct_name] : std::views::zip(bodies, struct_names))
      result += std::format(
          "if (svarg::match_content_type<{}>(content_type)) return vtor.template operator()<{}>();\n",
          render_string(b.contentType.str()), struct_name);
    result += std::format("return vtor.template operator()<void>();\n");
    result += "}\n\n";
  } else {
    // одно тело
    auto& body = *bodies.begin();
    auto [struct_name, str] = render_body(body, spec, jp);
    result += str;
    if (body.required)
      result += std::format("\n{} body;\n", struct_name);
    else
      result += std::format("\n{}<{}> body;\n", optional_str, struct_name);
  }

  return result;
}

// генерирует
// struct default_response|response_{status} {
//   int status = 0; // static constexpr если статус известен заранее
//   struct headers_t { ... }
//   headers_t headers; // если есть хедера
//   body_A, body_B... // если есть боди
//   body_t body;
// };
rendered_with_name renderer::render_response(const ir::response_desc& rsp, std::string_view spec,
                                             const json_pointer& jp) const {
  std::string result;
  std::string struct_name =
      rsp.is_default() ? "default_response" : std::format("response_{}", rsp.status.to_string());

  result += std::format("struct {} {{\n\n", struct_name);

  result += render_parse_tag(cxx_support::parse_tag_e::response);

  if (!rsp.is_default()) {
    if (!rsp.is_range())
      // имя совпадает с полем, которое не генерируется когда есть чёткий статус
      result += std::format("static constexpr int status = {};\n\n", rsp.status.to_string());
    else
      result += std::format("static constexpr status_range_e status_range = status_range_e::{};\n\n",
                            rsp.status.to_string());
  }
  if (rsp.is_default() || rsp.is_range())
    result += "int status = 0;\n";
  if (rsp.headers && !rsp.headers->empty()) {
    std::vector<ir::operation_parameter> params;
    for (auto& h : *rsp.headers) {
      h.validate();
      params.push_back(ir::operation_parameter{
          .location = parameter_location::header,
          .name = h.name,
          .schema = h.schema ? *h.schema : ir::make_fundamental_ref(ir::fundamental_e::string),
          .required = h.required,
          .encoding = h.encoding,
      });
    }
    result += "struct headers_t {\n";
    result += render_parameters(params, spec, jp, *this);
    result += "};\n";
    result += "headers_t headers;\n";
  }
  if (rsp.bodies)
    result += render_bodies(*rsp.bodies, spec, jp);
  result += "};\n\n";
  return {std::move(struct_name), std::move(result)};
}

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

struct build_path_make_fn_visitor {
  std::string& out;
  size_t param_index = 0;
  const std::vector<const ir::operation_parameter*>& params;
  const render_options& options;

  explicit build_path_make_fn_visitor(std::string& x, std::vector<const ir::operation_parameter*>& params,
                                      const render_options& options)
      : out(x), params(params), options(options) {
    out += "static constexpr void visit_path(auto&& vtor) {";
  }

  void substr(std::string_view sv) {
    out += std::format("vtor.substr({});\n", render_string(sv));
  }
  void param(std::string_view param_name) {
    assert(param_name == params[param_index]->name);
    out += std::format("vtor.param(std::index_sequence<{}>{{}});\n", param_index);
    ++param_index;
  }
  void param(std::string_view param_name, std::string_view sep) {
    assert(param_name == params[param_index]->name);
    out += std::format("vtor.param(std::index_sequence<{}>{{}}, {});\n", param_index, render_string(sep));
    ++param_index;
  }

  void pattern_end() {
    out += "}\n\n";
  }
  void error(std::string_view msg) {
    throw render_error("{}", msg);
  }

  static consteval bool is_done() {
    return false;
  }
};

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
  result += std::format("struct {} {{\n", make_cxx_ident(op.id, options));

  // parse tag
  result += render_parse_tag(cxx_support::parse_tag_e::operation);

  // spec
  result += std::format("static constexpr inline std::string_view spec = {};\n\n", render_string(op.spec));

  // method

  // uppered так как все методы в регистре методов должны быть upper (GET/POST а в опенапи файле пишутся
  // маленькими буквами). Игнорируем что какой-то кастомный метод зачем быть GeT
  result += std::format("static constexpr inline std::string_view method = {};\n",
                        render_string(uppered(op.method)));
  std::string path = deescape_path(op.path);
  // parameters
  if (op.has_path()) {
    // static constexpr void visit_path(auto&& vtor)
    visit_templated_path_parts(path, build_path_make_fn_visitor{result, path_params, options});
    result += std::format("static constexpr std::string_view path_expr = {};\n\n", render_string(path));
  }
  auto deref = std::views::transform([](auto* p) -> auto& { return *p; });
  if (!path_params.empty()) {
    if (!op.has_path())
      throw render_error("operation have path parameters while not having path");
    for (const ir::operation_parameter& p : path_params | deref) {
      if (!path.contains("{" + p.name + "}")) {
        throw render_error(
            "operation `{}` contains parameter `{}` which is not present in path expression `{}`", op.id,
            p.name, path);
      }
    }
    result += "struct path_t {\n";
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

  if (op.responses || op.defaultResponse) {
    // имена структур ответов в порядке op.responses
    std::vector<std::string> struct_names;
    if (op.responses) {
      for (auto& r : *op.responses) {
        assert(!r.is_default());
        auto [struct_name, rendered] = render_response(r, op.spec, paths);
        result += rendered;
        struct_names.push_back(std::move(struct_name));
      }
    }
    static const std::remove_cvref_t<decltype(*op.responses)> no_responses;
    const auto& responses = op.responses ? *op.responses : no_responses;
    auto statuses = [&](bool ranges) {
      return std::views::zip(struct_names, responses) |
             std::views::filter([ranges](auto&& x) { return std::get<1>(x).is_range() == ranges; });
    };

    result += "static constexpr decltype(auto) visit_response_status(int status, auto&& vtor) {\n";
    // сначала не ренжи (2ХХ)
    for (auto&& [struct_name, r] : statuses(/*ranges=*/false)) {
      result += std::format("if (status == {}) return vtor.template operator()<{}>();\n",
                            r.status.to_string(), struct_name);
    }
    // затам ренжи, чтобы более специализированный был проверен раньше
    for (auto&& [struct_name, r] : statuses(/*ranges=*/true)) {
      result += std::format(
          "if (svarg::match_status_range<status_range_e::{}>(status)) return vtor.template "
          "operator()<{}>();\n",
          r.status.to_string(), struct_name);
    }
    if (op.defaultResponse)
      result += "return vtor.template operator()<default_response>();\n";
    else
      result += "return vtor.template operator()<void>();\n";
    result += "}\n\n";

    // (имя структуры, суффикс get_if_)
    std::vector<std::pair<std::string, std::string>> results;
    for (auto&& [struct_name, r] : std::views::zip(struct_names, responses))
      results.emplace_back(struct_name, r.status.to_string());
    if (op.defaultResponse)
      results.emplace_back("default_response", "default");

    if (results.size() == 1) {
      result += std::format("using result_t = {};\n", results.front().first);
    } else {
      result += "struct result_t {\n";
      auto names_rng = results | std::views::keys | std::views::join_with(',');
      result +=
          std::format("\n{}<{}> data;\n\n", variant_str, std::string(names_rng.begin(), names_rng.end()));
      for (auto& [struct_name, suffix] : results) {
        result +=
            std::format("const {0}* get_if_{1}() const noexcept {{ return std::get_if<{0}>(&data); }}\n",
                        struct_name, suffix);
        result += std::format("{0}* get_if_{1}() noexcept {{ return std::get_if<{0}>(&data); }}\n",
                              struct_name, suffix);
      }
      result += "};\n";
    }
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

// CLIENT/SERVER

// путь из url сервера без завершающего '/', переменные заменяются значениями по умолчанию
// "https://api.example.com/v1/" -> "/v1", "/" -> ""
static std::string server_base_path(const server_object& s) {
  std::string url;
  std::string_view tmpl = s.url;
  while (!tmpl.empty()) {
    size_t open = tmpl.find('{');
    url += tmpl.substr(0, open);
    if (open == tmpl.npos)
      break;
    size_t close = tmpl.find('}', open);
    if (close == tmpl.npos)
      throw render_error("invalid server url `{}`", s.url);
    std::string_view name = tmpl.substr(open + 1, close - open - 1);
    auto var = s.variables ? s.variables->find(name) : decltype(s.variables->find(name)){};
    if (!s.variables || var == s.variables->end())
      throw render_error("server url `{}` uses undefined variable `{}`", s.url, name);
    url += var->second.default_;
    tmpl.remove_prefix(close + 1);
  }
  std::string_view path = url;
  if (!is_relative_url(url))
    path = parse_absolute_url(url).path;
  path = path.substr(0, path.find_first_of("?#"));
  while (path.ends_with('/'))
    path.remove_suffix(1);
  if (!path.empty() && !path.starts_with('/'))
    throw render_error("server url `{}` has path not starting with '/'", s.url);
  return std::string(path);
}

namespace {

// операция по полному пути (base path сервера + путь операции)
struct route {
  const ir::operation* op;
  std::string base_path;
  std::string path;  // включая base_path
};

}  // namespace

// TODO теоретически пути делятся на "точное соответствие" - хеш мапа и для динамических и для статических,
// без аргументов пути и может префиксное дерево для остального. Но существуют неоднозначности

// возвращает сортированных набор маршрутов такой, что по нему можно идти сверху вниз при матче path
// и первый сматченный будет наиболее специализированным
// возвращает мапу метод -> маршруты (сначала не шаблонные пути, затем шаблонные сортированные по приоритету)
static string_map<std::vector<route>> order_routes_for_path_matching(std::vector<route> routes) {
  auto is_not_templated = [](const route& r) { return !is_template_path(r.path); };
  // TODO такую неоднозначность (см. ниже) можно решать по правилу точное совпадение выигрывает. В данном
  // случае v1 это точное совпадение с пришедшей строкой, а значит выигрывает она?

  // приоритет важен для предсказуемости выбора в неоднозначных ситуациях
  // /v1/{a} и /{version}/x могут сматчить одну и ту же строку "/v1/x"
  // хотя среди них нет более специализированного
  // для разрешения этой ситуации используется приоритет,
  // в опенапи приоритет выше если операция написана выше в спеке

  auto by_priority = [](const route& a, const route& b) {
    // меньше приоритет - выше в списке. У всех должен быть назначен приоритет, так как вебхуки не должны
    // быть присоединены к серверу
    return a.op->priority.value() < b.op->priority.value();
  };
  auto by_specialization = [by_priority](const route& a, const route& b) {
    auto order = compare_paths_specialized(a.path, b.path);
    if (order != std::strong_ordering::equivalent)
      // в начале самые приоритетные
      return order == std::strong_ordering::greater;
    return by_priority(a, b);
  };

  // метод -> маршруты
  string_map<std::vector<route>> ordered;
  for (auto& r : routes)
    ordered[r.op->method].push_back(std::move(r));
  // сортируем внутри вектора по специализации и приоритету
  for (auto& [method, rs] : ordered) {
    // сначала не шаблонизированные пути
    auto templated = std::ranges::partition(rs, is_not_templated);
    std::ranges::stable_sort(rs.begin(), templated.begin(), by_priority);
    std::ranges::stable_sort(templated, by_specialization);
  }
  return ordered;
}

std::string renderer::render_server(const std::vector<const ir::operation*>& operations,
                                    std::string_view spec) const {
  assert(!operations.empty());

  std::string result = "#pragma once\n\n#include \"common_types.hpp\"\n\n";
  for (auto* op : operations)
    result += std::format("#include \"{}\"\n", make_file_path(*op, options, /*is_webhook=*/false));
  result += std::format("namespace {} {{\n\n", make_namespace(spec, {}, options));

  // TODO x-name для сервера поддержать ?
  result += "struct server : hidi::h2server {\nusing hidi::h2server::h2server;\n\n";

  auto make_handler_method_name = [&](const ir::operation& op) {
    return std::format("do_{}", make_cxx_ident(op.id, options));
  };

  // виртуальные функции операций

  for (auto* op : operations) {
    assert(op);
    // возвращает http_response чтобы пользователь мог гибче назначить всякие параметры content type
    // или сделать что-то неподдерживаемое библиотекой
    result += std::format(
        "virtual dd::task<hidi::http_response> {}({}, hidi::http_request, hidi::request_context) = "
        "0;\n\n",
        make_handler_method_name(*op), make_cxx_ident(op->id, options));
  }

  result +=
      "// путь и метод не подошли ни к одной операции\n"
      "virtual dd::task<hidi::http_response> on_unknown_operation(hidi::http_request, "
      "hidi::request_context) {\n"
      "co_return hidi::http_response{.status = 404};\n}\n\n"
      "// операция найдена, но запрос не соответствует ей (параметры, тело...)\n"
      "virtual dd::task<hidi::http_response> on_bad_request(hidi::http_request, svarg::error_code, "
      "hidi::request_context) {\n"
      "co_return hidi::http_response{.status = 400};\n}\n\n";

  result +=
      "dd::task<hidi::http_response> handle_request(hidi::http_request req, hidi::request_context ctx) "
      "override {\n"
      "std::string_view path = svarg::path_and_query(req.path).first;\n";

  // операция доступна по base path каждого своего сервера
  std::vector<route> routes;
  for (auto* op : operations) {
    std::set<std::string> base_paths;
    for (auto& srv : *op->servers)
      base_paths.insert(server_base_path(srv));
    for (auto& bp : base_paths)
      routes.push_back(route{.op = op, .base_path = bp, .path = bp + deescape_path(op->path)});
  }

  result += "switch(req.method) {\n";
  for (auto& [method, rs] : order_routes_for_path_matching(std::move(routes))) {
    result += std::format("case hidi::http_method_e::{}:\n", method);
    for (auto& r : rs) {
      result += std::format(
          "if (svarg::match_path_pattern<{}>(path)) {{\n"
          "{} op;\n"
          "svarg::error_code ec = svarg::cxx_support::from_request(req, op, {});\n"
          "if (ec) return on_bad_request(std::move(req), std::move(ec), std::move(ctx));\n"
          "return {}(std::move(op), std::move(req), std::move(ctx));\n"
          "}}\n",
          render_string(r.path), make_cxx_ident(r.op->id, options), render_string(r.base_path),
          make_handler_method_name(*r.op));
    }
    result += "break;\n";
  }
  result += "default: break;\n}\n";  // end switch
  result += "return on_unknown_operation(std::move(req), std::move(ctx));\n";
  result += "}\n";        // end fn
  result += "};\n\n}\n";  // end struct, namespace
  return result;
}

std::string renderer::render_client(const std::vector<const ir::operation*>& operations,
                                    std::string_view spec) const {
  assert(!operations.empty());
  std::string res;

  res += "#pragma once\n\n#include \"common_types.hpp\"\n\n";
  // include необходимых операций
  for (auto* op : operations)
    res += std::format("#include \"{}\"\n", make_file_path(*op, options, /*is_webhook=*/false));

  res += std::format("namespace {}\n {{", make_namespace(spec, {}, options));

  // клиент один и он в неймспейсе спеки
  res += "struct client {";
  res += "svarg::cxx_support::h2clients_pool& pool;\n";

  for (auto* op : operations) {
    auto name = make_cxx_ident(op->id, options);
    // TODO нормальный url + дефолтное значение
    std::string get_addr = std::format("{{ hidi::endpoint(host), {{}} }}");
    res += std::format(
        "dd::task<svarg::cxx_support::operation_result<{}>> send_{}({} op, std::string host, "
        "hidi::deadline_t "
        "deadline, "
        "svarg::content_type_params ctp "
        "= {{}}) const {{\n return "
        "svarg::cxx_support::send_request<{}>(*pool.get_connection_to({}), "
        "svarg::cxx_support::make_request(op, "
        "ctp), deadline); "
        "}}\n",
        name, name, name, name, get_addr);
  }

  res += "};\n\n";  // end struct

  res += "}\n";  // end namespace
  return res;
}

}  // namespace svarg::cxx
