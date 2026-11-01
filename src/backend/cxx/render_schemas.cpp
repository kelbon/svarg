// СХЕМЫ: файл схемы, типы полей, зависимости, значения по умолчанию

#include "render_common.hpp"

namespace svarg::cxx {

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

  auto collect = [&](const ir::schema_ref& ref) {
    if (ref != s && !ref.is_fundamental())  // рекурсия
      out.insert(ref);  // если ref уже там был, то автоматически обрабатывается
  };
  // схемы-требования упоминаются в required_schemas, ограничения рендерятся внутрь и файла не имеют
  if (t.required) {
    for (const ir::schema_ref& r : *t.required)
      if (r.is_fundamental() || !schemaof(r).type.is_constraints())
        collect(r);
  }

  if (t.type.is_fundamental())
    return;

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
        // схемы хедеров частей multipart рендерятся внутрь
        if (const ir::body* b = find_body_encodings(t); b && b->membersHeaders)
          for (auto& [_, headers] : *b->membersHeaders)
            for (auto& h : headers)
              if (h.schema)
                collect(*h.schema);
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

  std::string ns = make_namespace(t.spec, t.jsonptr, relspec, relative);
  std::string name = get_schema_name(s, options);
  std::string result = ns.empty() ? std::string(name) : std::format("{}::{}", ns, name);
  return {.typeref = std::move(result), .is_alias = false};
}

bool renderer::is_recursive_field(const ir::schema_ref& owner, const ir::schema_ref& field_type) const {
  // поле типа самого владельца (Node { Node child; }), collect_all_mentioned не видит прямую
  // ссылку на себя
  if (field_type == owner)
    return true;
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

// инициализатор поля значением по умолчанию: литерал для фундаментальных типов, иначе разбор JSON
// один раз (default_value)
std::string renderer::render_default_value(const ir::schema_ref& type, const Any& value,
                                           std::string_view relspec, const json_pointer& relative) const {
  const boost::json::value& v = value.value;
  auto invalid = [&] {
    return render_error("default `{}` does not match type `{}`", boost::json::serialize(v),
                        render_schema_ref(type, relspec, relative).typeref);
  };
  if (std::optional<ir::fundamental_e> f = type.if_fundamental()) {
    switch (*f) {
      case ir::fundamental_e::boolean:
        if (!v.is_bool())
          throw invalid();
        return v.get_bool() ? "true" : "false";
      case ir::fundamental_e::integer:
        if (v.is_int64())
          return std::format("::svarg::cxx_support::integer({})", v.get_int64());
        throw invalid();
      case ir::fundamental_e::number:
        if (v.is_int64())
          return std::format("::svarg::cxx_support::number(int64_t({}))", v.get_int64());
        if (v.is_uint64())
          return std::format("::svarg::cxx_support::number(uint64_t({}ull))", v.get_uint64());
        if (v.is_double())
          return std::format("::svarg::cxx_support::number(double({}))", v.get_double());
        throw invalid();
      case ir::fundamental_e::string:
        if (!v.is_string())
          throw invalid();
        return std::format("std::string({})", render_string(std::string_view(v.get_string())));
      default:
        break;
    }
  }
  return std::format("::svarg::cxx_support::default_value<{}, {}>()",
                     render_schema_ref(type, relspec, relative).typeref,
                     render_string(boost::json::serialize(v)));
}

// имя вложенной структуры хедеров части multipart для свойства
static std::string part_headers_struct_name(std::string_view property, const render_options& options) {
  return std::format("{}_part_headers", make_cxx_ident(property, options));
}

// хедеры частей multipart (encoding.headers) - вложенные структуры объекта тела
std::string renderer::render_part_headers(const string_map<std::set<ir::header>>& part_headers,
                                          std::string_view relspec, const json_pointer& relative) const {
  std::string result;
  for (auto& [property, headers] : part_headers) {
    std::vector<ir::operation_parameter> params;
    for (auto& h : headers)
      params.push_back(header_as_parameter(h));
    result += std::format("struct {} {{\n", part_headers_struct_name(property, options));
    result += render_parameters(params, relspec, relative);
    result += "};\n\n";
  }
  return result;
}

void renderer::collect_body_encodings() {
  auto add = [&](const std::set<ir::body>& bodies) {
    for (const ir::body& b : bodies) {
      bool has_encodings = (b.membersEncoding && !b.membersEncoding->empty()) ||
                           (b.membersHeaders && !b.membersHeaders->empty());
      if (!has_encodings || b.schema.is_fundamental())
        continue;
      // одна схема в нескольких телах - encoding берётся из первого
      const ir::schema& s = schemaof(b.schema);
      body_encodings.try_emplace(std::pair(s.spec, std::string(s.jsonptr.escaped())), &b);
    }
  };
  for (auto* ops : {&ir.operations, &ir.webhooks}) {
    if (!*ops)
      continue;
    for (const ir::operation& op : **ops) {
      if (op.bodies)
        add(*op.bodies);
      if (op.responses)
        for (auto& r : *op.responses)
          if (r.bodies)
            add(*r.bodies);
      if (op.defaultResponse && op.defaultResponse->bodies)
        add(*op.defaultResponse->bodies);
    }
  }
}

const ir::body* renderer::find_body_encodings(const ir::schema& s) const {
  auto it = body_encodings.find(std::pair(s.spec, std::string(s.jsonptr.escaped())));
  return it == body_encodings.end() ? nullptr : it->second;
}

std::string renderer::render_object_field(const ir::object_field_info& field, std::string_view relspec,
                                          const json_pointer& relative, bool recursive_field) const {
  if (recursive_field && field.required) {
    throw render_error("field `{}` in type `{}#{}` recursive and required", field.name, relspec,
                       relative.escaped());
  }
  // поле со значением по умолчанию присутствует всегда (рекурсивное остаётся опциональным)
  if (field.default_value && !recursive_field) {
    return std::format("{} = {}", render_required_field(field.type, field.name, relspec, relative, false),
                       render_default_value(field.type, *field.default_value, relspec, relative));
  }
  if (field.required)
    return render_required_field(field.type, field.name, relspec, relative, recursive_field);
  return render_optional_field(field.type, field.name, relspec, relative, recursive_field);
}

std::string renderer::render_object_fields(const arrayof<ir::object_field_info>& fields,
                                           std::string_view relspec, const json_pointer& relative,
                                           std::optional<ir::schema_ref> fields_owner,
                                           std::optional<ir::schema_ref> additional_properties,
                                           const string_map<std::set<ir::header>>* part_headers,
                                           const string_map<field_render_info>* extra) const {
  auto is_recursive = [&](const ir::schema_ref& r) {
    if (r.is_fundamental())
      return false;
    return fields_owner ? is_recursive_field(*fields_owner, r) : false;
  };
  auto render_field = [&](const ir::object_field_info& f) {
    // часть multipart с хедерами: part<T, хедеры>
    if (part_headers && part_headers->contains(f.name)) {
      std::string type = std::format("::svarg::cxx_support::part<{}, {}>",
                                     render_schema_ref(f.type, relspec, relative).typeref,
                                     part_headers_struct_name(f.name, options));
      if (f.required)
        return std::format("{} {};\n", type, make_cxx_ident(f.name, options));
      return std::format("{}<{}> {};\n", optional_str, type, make_cxx_ident(f.name, options));
    }
    return render_object_field(f, relspec, relative, is_recursive(f.type)) + ";\n";
  };

  std::string result;
  for (auto&& f : fields) {
    if (f.required)
      result += render_field(f);
  }
  // затем опциональные
  for (auto&& f : fields) {
    if (!f.required)
      result += render_field(f);
  }
  if (!additional_properties || additional_properties->is_fundamental(ir::fundamental_e::never)) {
    result += std::format("\nusing {} = void;\n\n", cxx_support::additional_properties_schema_alias_name);
  } else {
    result += std::format("\nusing {} = {};\n\n", cxx_support::additional_properties_schema_alias_name,
                          render_schema_ref(*additional_properties, relspec, relative).typeref);
    // TODO обработать возможное пересечение имён с обычными полями
    result += std::format("::svarg::cxx_support::additional_properties_t<{}> {};\n\n",
                          cxx_support::additional_properties_schema_alias_name,
                          cxx_support::additional_properties_field_name);
  }
  // описания полей в порядке объявления (сначала требуемые, затем опциональные): имена из спецификации
  // (C++ имена могут не совпадать), обязательность, медиа тип значения
  auto field_info = [&](const ir::object_field_info& f) {
    std::string r = std::format("{{.name = {}", render_string(f.name));
    if (f.required)
      r += ", .required = true";
    const field_render_info* info = nullptr;
    if (extra)
      if (auto it = extra->find(f.name); it != extra->end())
        info = &it->second;
    if (info && info->encoding)
      r += std::format(", .encoding = {}", render_string(*info->encoding));
    if (info && info->explode)
      r += std::format(", .explode = {}", *info->explode);
    return r + "},\n";
  };
  result += std::format("static constexpr std::array<::svarg::cxx_support::field_info, {}> fields = {{{{\n",
                        fields.size());
  for (auto&& f : fields)
    if (f.required)
      result += field_info(f);
  for (auto&& f : fields)
    if (!f.required)
      result += field_info(f);
  result += "}};\n\n";
  // доступ к полю по индексу (в порядке fields) без boost::pfr, который дорог для компиляции
  if (!fields.empty()) {
    result +=
        "static constexpr bool has_svarg_field = true;\n"
        "template <size_t I>\n"
        "[[nodiscard]] constexpr decltype(auto) svarg_field(this auto&& self) noexcept {\n";
    size_t i = 0;
    auto add = [&](const ir::object_field_info& f) {
      result += std::format(
          "{}if constexpr (I == {}) return ::svarg::noadl::forwardLike<decltype(self)>(self.{});\n",
          i == 0 ? "" : "else ", i, make_cxx_ident(f.name, options));
      ++i;
    };
    for (auto&& f : fields)
      if (f.required)
        add(f);
    for (auto&& f : fields)
      if (!f.required)
        add(f);
    result += "else static_assert(false);\n}\n\n";
  }
  return result;
}

// `typeref` - тип поля, если s полутривиальный объект
std::string renderer::render_struct_content(const ir::schema& s, std::string typeref) const {
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
    return render_parse_tag(tag) + render_special_field(r) + render_requirements();
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
        if (is_string_enum && e.open && *e.open)
          return render_open_string_enum(e, s);
        if (is_string_enum) {
          result += render_parse_tag(cxx_support::parse_tag_e::string_enum);
          result += std::format("static constexpr inline std::array<std::string_view, {}> values = {{",
                                e.values.size());
          // строковое значение - как есть, остальное (см. ниже) - сериализованным
          auto as_text = [](const boost::json::value& v) -> std::string {
            if (v.is_string())
              return std::string(v.get_string());
            return boost::json::serialize(v);
          };
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
            result += std::format("{}, ", render_string(as_text(v.value)));
          result += "};\n";  // end values
          result += "enum struct values_e {";
          for (auto&& v : e.values) {
            // боюсь представить идентификатор созданный из какого-то объекта, но ты сам виноват если такое
            // делаешь
            result += std::format("{},", make_cxx_ident(as_text(v.value), options));
          }
          result += "};\nusing enum values_e;\n\n";  // end values_e
          // возможное пересечение имён using enum / special_field_name игнорируется
          result += render_special_field("values_e");
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
          result += render_special_field(type);
        }
        return result;
      },
      [&](const ir::object_schema& a) -> std::string {
        std::string result = render_parse_tag(cxx_support::parse_tag_e::object);
        const ir::body* body = find_body_encodings(s);
        const string_map<std::set<ir::header>>* part_headers =
            body && body->membersHeaders ? &*body->membersHeaders : nullptr;
        if (part_headers)
          result += render_part_headers(*part_headers, s.spec, s.jsonptr);
        string_map<field_render_info> extra;
        if (body && body->membersEncoding)
          for (auto& [prop, ct] : *body->membersEncoding)
            extra[prop].encoding = ct.str();
        result += render_object_fields(a.fields, s.spec, s.jsonptr, /*fields_owner=*/s.ref(),
                                       a.additional_properties, part_headers, &extra);
        return result;
      },
  };
  return std::visit(vtor, s.type.data) + render_requirements();
}

// открытый enum строк: строка `value` и известные значения константами (NFType::AMF). Константа, имя которой
// совпало с членом структуры или другой константой, получает суффикс '_'
std::string renderer::render_open_string_enum(const ir::enum_schema& e, const ir::schema& s) const {
  std::string name = get_schema_name(s, options);
  std::string result = render_parse_tag(cxx_support::parse_tag_e::open_string_enum);
  result += "// известные значения, допустима любая строка\n";
  std::set<std::string> used = {"value", "values", "is_known", "parse_tag", name};
  std::vector<std::string> consts;
  for (auto&& v : e.values) {
    std::string text =
        v.value.is_string() ? std::string(v.value.get_string()) : boost::json::serialize(v.value);
    std::string ident = make_cxx_ident(text, options);
    while (!used.insert(ident).second)
      ident += '_';
    result += std::format("static constexpr std::string_view {} = {};\n", ident, render_string(text));
    consts.push_back(std::move(ident));
  }
  auto names = consts | std::views::join_with(std::string_view(", "));
  result += std::format("static constexpr std::array<std::string_view, {}> values = {{{}}};\n\n",
                        consts.size(), std::string(names.begin(), names.end()));
  result += "std::string value;\n\n";
  result += std::format(
      "{0}() = default;\n"
      "// неявный: p.field = {0}::X, p.field = \"any\"\n"
      "template <typename S>\n"
      "  requires std::constructible_from<std::string, S>\n"
      "{0}(S&& s) : value(std::forward<S>(s)) {{\n}}\n\n"
      "template <typename S>\n"
      "  requires std::convertible_to<const S&, std::string_view>\n"
      "bool operator==(const S& v) const noexcept {{\nreturn value == std::string_view(v);\n}}\n"
      "bool operator==(const {0}&) const = default;\n\n"
      "// одно из известных значений\n"
      "bool is_known() const noexcept {{\nreturn std::ranges::contains(values, value);\n}}\n",
      name);
  return result;
}

render_result renderer::render_schema(const ir::schema& s) const {
  std::string file_begin = render_file_begin();
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
    // при взаимной рекурсии схем (A -> массив B -> A) include одной из них пропускается pragma once,
    // поэтому зависимости объявляются заранее (массивы и box допускают неполный тип)
    for (auto& ref : deps) {
      file_begin += std::format("\nnamespace {} {{ struct {}; }}", make_namespace(ref.spec, *ref.jsonptr),
                                get_schema_name(ref, options));
    }
    file_begin += std::format("\nnamespace {} {{\n\n", make_namespace(s.spec, s.jsonptr));
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
