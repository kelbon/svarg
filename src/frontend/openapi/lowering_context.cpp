#include "svarg/frontend/openapi/lowering_context.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/ir/ir_visitor.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/merge_with_override.hpp"
#include "svarg/utils/json_pointer.hpp"
#include "svarg/utils/guard_array.hpp"
#include "svarg/utils/for_each_field.hpp"
#include "svarg/backend/cxx/cxx_ident.hpp"  // to_cxx_ident
#include "svarg/x_meta_keys.hpp"

#include <boost/pfr.hpp>

namespace svarg {

#ifndef NDEBUG
template <typename T>
static bool parsable_as(const boost::json::value* j) {
  if (!j)
    return false;
  try {
    from_json<T>(*j);
    return true;
  } catch (...) {
    return false;
  }
}
#endif

static std::optional<std::string> try_get_name(const json_pointer& jp) {
  // ссылаться разрешено только на то что находится в schemas,
  // т.е. что-то типа /components/schemas/User => User
  if (!jp.view().starts_with("/components/schemas/") || jp.segments_count() != 3)
    return std::nullopt;
  return jp.last_segment();
}

// посещает все значения с ключом $ref в json
static void visit_refs(boost::json::value& val, auto&& visitor) {
  switch (val.kind()) {
    case boost::json::kind::object: {
      for (auto& [key, child] : val.get_object()) {
        if (key == "$ref")
          visitor(child);
        else
          visit_refs(child, visitor);
      }
      break;
    }
    case boost::json::kind::array: {
      for (auto& elem : val.get_array())
        visit_refs(elem, visitor);
      break;
    }
    default:
      break;
  }
}

// curfile - файл внутри которого написаны ссылки в fields (куда ссылается например локальное #ref)
// ctx - используется чтобы найти существующие файлы, т.е. учесть инклуд директории
// Note: ссылки ремапятся в абсолютные пути, хотя это и запрещено в $ref. Но у меня поддержано чтобы не
// возиться
// Нужно так как в цепи ссылок $ref написаны в одном файле, а резолвится будут возможно в контексте
// другого поэтому их нужно перемапить на "универсальную" основу не зависящую от конкретного файла
static void remap_references(boost::json::value& obj, const fs::path& curfile, const lowering_context& ctx) {
  auto remap_ref = [&](boost::json::value& ref) {
    if (!ref.is_string())
      throw parse_error("invalid reference, expected string, but got `{}`", e2str(ref.kind()));
    parsed_ref r = parse_ref(ref.as_string());
    fs::path p = ctx.find_file_from_ref(r.file, curfile);
    ref.as_string() = std::format("{}#{}", p.generic_string(), r.json_ptr.escaped());
  };
  visit_refs(obj, remap_ref);
}

static void remap_references(string_map<Any>& fields, const fs::path& curfile, const lowering_context& ctx) {
  for (auto& [key, a] : fields)
    remap_references(a.value, curfile, ctx);
}

struct references_chain {
  fs::path file;
  json_pointer jp;
  lowering_context& ctx;
  // сумма перекрытых всей цепочкой ссылок полей
  // (всегда с ремапленными ссылками)
  //  $ref + override FIELD1 ссылается на $ref + override FIELD2 и тд
  string_map<Any> override_fields;
  // любая ссылка с override филдами в итоге должна сгенерировать свой уникальный тип
  // например:
  //  $0     $1                $2
  //  ref -> ref + override -> schema
  //  $0 это ссылка на $1, $1 это уникальный тип на который можно ссылаться
  // иначе несколько ссылок могут ссылаться на один объект не смотря на разную итоговую схему
  std::optional<std::pair<fs::path, json_pointer>> first_with_overrides;

  references_chain(fs::path file, json_pointer jp, lowering_context& ctx)
      : file(std::move(file)), jp(std::move(jp)), ctx(ctx) {
  }

  boost::json::value follow(reference_object ref) {
    if (!first_with_overrides && !ref._keyfield_override_fields.empty())
      first_with_overrides.emplace(file, jp);
    remap_references(ref._keyfield_override_fields, file, ctx);
    override_fields =
        merge_with_override(std::move(override_fields), std::move(ref._keyfield_override_fields));
    {
      parsed_ref r = parse_ref(ref.ref_);
      file = ctx.find_file_from_ref(r.file, file);
      jp = std::move(r.json_ptr);
    }
    const boost::json::value* pointed = jp.access(ctx.load_as_json(file));
    if (!pointed)
      throw semantic_error("invalid json pointer `{}`, does not refer to value in `{}`", jp.unescaped(),
                           file.generic_string());
    boost::json::value refered = *pointed;
    if (!refered.is_object())
      throw semantic_error("refered by $ref must be json/yaml object, but got {}", e2str(refered.kind()));
    // объект всегда должен возвращаться с универсальными ссылками, чтобы избежать смешения ссылок из разных
    // контекстов например конфликт контекста "первый $ref + overrided" и "конечный объект без $ref"
    remap_references(refered, file, ctx);
    // в override_fields ссылки уже универсальные
    for (auto& [k, j] : override_fields)
      refered.as_object()[k] = j.value;
    return refered;
  }
};

// TODO тест на ремап ссылок в resolve_ref_to учитывая инклуд директории и так далее
// TODO в IR складывать и спеку и джсон птр где лежало значение

// ref - ссылка по которой нужно пройти
// file - файл где эта ссылка лежит
// jp - указатель в file где эта ссылка лежит
// возвращает итоговый объект, файл в котором он лежал и джсон поинтер на него
// Note: объект может не существовать в входном апи, а быть сгенерированным через $ref с перекрытыми полями
// Note: в вернувшемся объекте все ссылки универсальные (относительно specs dir)
template <typename T>
object_and_path<T> resolve_ref_to(reference_object ref, fs::path file, json_pointer jp,
                                  lowering_context& ctx) {
  // если обращение к nullptr - всё равно это ассёрт
  assert(from_json<reference_object>(*jp.access(ctx.load_as_json(file))) == ref);

  references_chain chain(std::move(file), std::move(jp), ctx);

  for (size_t recursion_depth = 0; recursion_depth <= ctx.opts.ref_max_depth; ++recursion_depth) {
    if (!ref.ref_.contains("#/components/")) [[unlikely]] {
      ctx.emit_warn(file, chain.jp.empty() ? parse_ref(ref.ref_).json_ptr : chain.jp,
                    "$ref should be into /components/*, but $ref = {}", ref.ref_);
    }
    boost::json::value json = chain.follow(std::move(ref));
    auto refered = from_json<reference_or<T>>(json);
    if (auto* value = refered.get_if_value()) {
      if (chain.first_with_overrides) {
        auto [reffile, refjp] = *std::move(chain.first_with_overrides);
        // нужно изменить само загруженное апи, чтобы json pointer на поле внутри $reef
        // с перекрытым полем стал валиден (т.к. он указывает внутрь $ref)
        ctx.replace_in_cache(reffile, refjp, std::move(json));
        return {std::move(*value), std::move(reffile), std::move(refjp)};
      } else
        return {std::move(*value), std::move(chain.file), std::move(chain.jp)};
    }
    ref = std::move(*refered.get_if_ref());
  }
  throw semantic_error("recursion depth max reached");
}

template <typename T>
object_and_path<T> resolve_ref_and_ret(reference_or<T> x, fs::path file, json_pointer jp,
                                       lowering_context& ctx) {
  if (auto* r = x.get_if_ref())
    return resolve_ref_to<T>(std::move(*r), std::move(file), std::move(jp), ctx);
  else
    return object_and_path(std::move(*x.get_if_value()), std::move(file), std::move(jp));
}

// jp используется только если это инлайн схема для создания уникального scope
[[nodiscard]] static resolved_schema resolve_schema(const reference_or<schema_object>& sor,
                                                    const fs::path& file, json_pointer jp,
                                                    lowering_context& ctx) {
  if (auto* ref = sor.get_if_ref()) {
    return ctx.resolve_schema_ref(*ref, file, std::move(jp));
  } else {
    auto s = ctx.schema_lowering(*sor.get_if_value(), file, std::move(jp), std::nullopt);
    return {std::move(s).ref(), *sor.get_if_value()};
  }
}

ir::object_schema lowering_context::make_object_type(const schema_object& schema, const fs::path& file,
                                                     json_pointer jp) {
  ir::object_schema ot;
  if (schema.properties) {
    if (schema.required) {
      for (const string& x : *schema.required) {
        if (!std::ranges::contains(*schema.properties | std::views::keys, x))
          emit_warn(file, jp, "required property `{}` does not appear in `properties`, ignored", x);
      }
    }
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      ir::object_field_info fi;
      fi.name = prop_name;
      auto [schemaref, field_schema] = resolve_schema(prop_sor, file, jp / "properties" / prop_name, *this);
      fi.type = schemaref;
      if (field_schema.default_ && !field_schema.nullable)
        fi.default_value = field_schema.default_;
      bool listed_required = schema.required && std::ranges::contains(*schema.required, prop_name);
      if (field_schema.nullable || field_schema.default_) {
        fi.required = false;
        if (listed_required) {
          emit_warn(file, jp, "property `{}` marked as required, but {}. `required` ignored", prop_name,
                    field_schema.nullable ? "has nullable type" : "has default");
        }
      } else if (schema.required) {
        if (field_schema.is_write_only() || field_schema.is_read_only()) {
          // readOnly/writeOnly означает, что по сути есть два типа - для запроса и ответа и required зависит
          // от этого здесь генерируется только один тип, поэтому оба readOnly/writeOnly означают по факту
          // опциональность поля
          emit_warn(file, jp, "property {} marked as {}, `required` ignored", prop_name,
                    field_schema.is_write_only() ? "writeOnly" : "readOnly");
        } else {
          for (const string& x : *schema.required) {
            if (prop_name == x)
              fi.required = true;
          }
        }
      }
      ot.fields.push_back(std::move(fi));
    }
  } else if (schema.required) {
    // неявно считаем будто properties определены с type: true
    for (auto& name : *schema.required) {
      ir::object_field_info fi;
      fi.name = name;
      fi.type = ir::make_fundamental_ref(ir::fundamental_e::any);
      fi.required = true;
      ot.fields.push_back(std::move(fi));
    }
  }
  if (schema.additionalProperties) {
    ot.additional_properties =
        resolve_schema(*schema.additionalProperties, file, jp / "additionalProperties", *this).ir_schema;
  } else if (!schema.properties) {
    // схема не описывает свойства (только required, как в anyOf: [{required: [fqdn]}, ...] у NFProfile, или
    // просто type: object) - ограничивает только их наличие, остальные свойства допустимы. Иначе требование
    // отвергло бы любой объект с другими полями
    ot.additional_properties = ir::make_fundamental_ref(ir::fundamental_e::any);
  } else {
    ot.additional_properties = opts.unsetted_additional_properties == false
                                   ? ir::make_fundamental_ref(ir::fundamental_e::never)
                                   : ir::make_fundamental_ref(ir::fundamental_e::any);
  }
  return ot;
}

ir::discriminator_desc lowering_context::discriminator_lowering(const discriminator_object& d,
                                                                const arrayof<ir::schema_ref>& schemas) {
  // дискриминатор оставляется даже если есть тривиальные схемы
  // бекенд может это считатЬ, например, дискриминаторов только для объектов в oneof/anyof
  // а для фундаментальных типов определять содержимое без помощи дискриминатора
  ir::discriminator_desc ird;
  ird.propertyName = d.propertyName;
  auto from_mapping_or_just_name = [&](std::string_view name) -> std::string_view {
    if (!d.mapping)
      return name;
    auto it = (*d.mapping).find(name);
    if (it == d.mapping->end())
      return name;
    return std::string_view(it->second);
  };
  for (auto& s : schemas) {
    if (!s.name)
      throw semantic_error("schema in anyOf/oneOf must have name");
    ird.mapping[std::string(from_mapping_or_just_name(*s.name))] = s;
  }
  return ird;
}

template <typename... Args>
[[nodiscard]] static size_t count_true(const auto&... args) noexcept {
  return (size_t(bool(args)) + ...);
}

static ir::fundamental_e detect_enum_type(const arrayof<Any>& values) noexcept {
  if (values.empty())
    return ir::fundamental_e::never;  // енам без значений
  if (std::ranges::all_of(values, [](const Any& v) { return v.value.is_string(); }))
    return ir::fundamental_e::string;
  else if (std::ranges::all_of(values,
                               [](const Any& v) { return v.value.is_int64() || v.value.is_uint64(); }))
    return ir::fundamental_e::integer;
  else if (std::ranges::all_of(values, [](const Any& v) { return v.value.is_number(); }))
    return ir::fundamental_e::number;
  else if (std::ranges::all_of(values, [](const Any& v) { return v.value.is_null(); }))
    return ir::fundamental_e::null;
  else if (std::ranges::all_of(values, [](const Any& v) { return v.value.is_bool(); }))
    return ir::fundamental_e::boolean;
  else
    return ir::fundamental_e::any;
}

// ключевые слова, которые превращаются в ir::constraints_schema
[[nodiscard]] static bool has_constraints(const schema_object& s) noexcept {
  return s.minLength || s.maxLength || s.pattern || s.minimum || s.maximum || s.exclusiveMinimum ||
         s.exclusiveMaximum || s.multipleOf || s.minItems || s.maxItems ||
         (s.uniqueItems && *s.uniqueItems) || s.minProperties || s.maxProperties;
}

[[nodiscard]] static ir::constraints_schema make_constraints(const schema_object& s) {
  ir::constraints_schema c;
  c.minLength = s.minLength;
  c.maxLength = s.maxLength;
  c.pattern = s.pattern;
  c.minimum = s.minimum;
  c.maximum = s.maximum;
  c.exclusiveMinimum = s.exclusiveMinimum;
  c.exclusiveMaximum = s.exclusiveMaximum;
  c.multipleOf = s.multipleOf;
  c.minItems = s.minItems;
  c.maxItems = s.maxItems;
  // uniqueItems: false ничего не требует
  if (s.uniqueItems && *s.uniqueItems)
    c.uniqueItems = true;
  c.minProperties = s.minProperties;
  c.maxProperties = s.maxProperties;
  return c;
}

[[nodiscard]] static std::optional<ir::schema_ref> try_simple(const schema_object& schema) {
  assert(!schema.allow_always() && !schema.allow_never());
  if (schema.enum_ || schema.const_ || schema.oneOf || schema.anyOf || schema.allOf || schema.not_)
    return std::nullopt;
  if (has_constraints(schema))
    return std::nullopt;
  if (schema.if_ || schema.then || schema.else_ || schema.dependentSchemas || schema.dependentRequired)
    return std::nullopt;
  if (schema.type) {
    if (schema.type->values.size() != 1)
      return std::nullopt;
    std::string_view type = schema.type->values.front();
    if (type == "object" || type == "array")
      return std::nullopt;

    if ((type == "string" || type == "integer" || type == "number") && schema.format)
      return std::nullopt;
    if (schema.nullable && *schema.nullable)
      return std::nullopt;
    ir::fundamental_e e;
    if (!efromstr(type, e))
      throw bad_enum_value<ir::fundamental_e>(type);
    return ir::make_fundamental_ref(e);
  }
  if (schema.nullable && *schema.nullable)
    return std::nullopt;
  // пустая схема типа type: {}
  return ir::make_fundamental_ref(ir::fundamental_e::any);
}

/*
- добавляет встреченные схемы в `schemas`. Как минимум один из schema + инлайн схемы
- file - путь до текущего файла (относительно директории со спеками)
- если name нет, то схема inline. Если есть, то на схему можно сослаться
Логика работы:
 * если схема true/false - остальные поля игнорируются (их не должно быть)
 * если есть type - он отвечает за представление типа,
   а возможные anyOf/oneOf/allOf/not/enum/const описывают требования
 * если нет type и есть const/enum/allOf/oneOf/allOf - то это они описывают представление.
 * not всегда описывает требование
 Если есть одновременно два и более из них - варнинг и все они будут требованиями, а представление будет Any
 * если ничего нет, но есть properties - то как будто неявно type: object
 * если нет ничего, то схема any, но возможно всё ещё с валидацией (н-р required [a, b, c])
*/
schema_or_ref lowering_context::schema_lowering(const schema_object& schema, const fs::path& file,
                                                json_pointer jp, std::optional<std::string_view> name) {
  ir::schema res;
  res.spec = file.filename().replace_extension().generic_string();
  res.jsonptr = jp;
  auto ret_schema = [&] {
    assert(parsable_as<schema_object>(jp.access(load_as_json(file))) && "invalidly generated scheme");
    result_ir.schemas->insert(res);
    return schema_or_ref(std::move(res));
  };
  if (name)
    res.name = *name;
  else
    res.name = std::nullopt;
  // META
  {
    auto _ = guard_array(res.meta);
    if (!schema.SVARG_META_KEYFIELD.empty()) {
      res.meta = schema.SVARG_META_KEYFIELD;
      if (!res.name && res.meta->contains(x_name_hint))
        res.name = from_json<std::string>((*res.meta)[std::string(x_name_hint)].value);
      if (auto it = res.meta->find(x_name_hint); it != res.meta->end())
        res.meta->erase(it);
      if (res.meta->empty())
        res.meta.reset();
    }
    if (schema.format)
      (*res.meta)[std::string(x_format_hint)] = Any(to_json(*schema.format));
  }
  if (auto it = result_ir.schemas->find(res); it != result_ir.schemas->end())
    return schema_or_ref{*it};
  // избегаем хранения jsonptr.view() (без escape возможно что совпадут пути "/" + "name" и "/name" одним
  // ключом)
  const std::string stored_scope = res.scope();
  const strpair cache_key{stored_scope, name ? *name : ""};
  if (schemas_in_progress.contains(cache_key))
    return schema_or_ref(res.ref());
  schemas_in_progress.insert(cache_key);
  on_scope_exit {
    schemas_in_progress.erase(cache_key);
  };
  // ── boolean schema ──────────────────────────────────────────────────────────
  if (schema.allow_always()) {
    res.type.data = ir::fundamental_e::any;
    return ret_schema();
  }
  if (schema.allow_never()) {
    res.type.data = ir::fundamental_e::never;
    return ret_schema();
  }
  // упрощение всяких type: string до фундаментальной ссылки
  if (std::optional ref = try_simple(schema))
    return schema_or_ref(std::move(*ref));
  res.type.data = ir::fundamental_e::any;  // позже заменится (или нет)

  // если нужно anyOf/... парсить как требования, а не как представление типа
  bool as_requirements = schema.enum_ || schema.type;

  // ── enum_ / const ─────────────────────────────────────────────────────────
  assert(!schema.const_);                        // postparse должен был удалить
  assert(!(!schema.type && schema.properties));  // postparse должен был обработать
  assert(!(!schema.type && schema.required));

  if (schema.enum_) {
    if (schema.enum_->empty()) {
      res.type.data = ir::fundamental_e::never;
    } else {
      ir::enum_schema es{.values = *schema.enum_};
      if (schema.type) {
        auto& types = schema.type->values;
        if (types.size() > 1 || (types.front() == "object" || types.front() == "array")) {
          // ссылаемся на тип определяемой прямо сейчас схемы
          es.type = res.ref();
        } else {
          ir::fundamental_e e;
          // types никогда не пустое
          if (!efromstr(types.front(), e))
            throw bad_enum_value<ir::fundamental_e>(types.front());
          es.type = ir::make_fundamental_ref(e);
        }
      } else {
        es.type = ir::make_fundamental_ref(detect_enum_type(es.values));
      }
      res.type.data = std::move(es);
    }
  } else if (schema.type) {
    arrayof<string> rtypes = schema.type->values;

    std::sort(rtypes.begin(), rtypes.end());
    rtypes.erase(std::unique(rtypes.begin(), rtypes.end()), rtypes.end());

    if (rtypes.size() == 1) {
      // Единственный тип — прямо в fundamental или object_type
      const auto& t = rtypes[0];

      if (t == "object") {
        res.type.data = make_object_type(schema, file, jp);
      } else if (t == "array") {
        ir::array_schema at;
        if (!schema.items)
          throw parse_error("schema with type 'array' must contain 'items'");
        at.element = resolve_schema(*schema.items, file, jp / "items", *this).ir_schema;
        res.type.data = std::move(at);
      } else {
        // Всё остальное — фундаментальный тип
        ir::fundamental_e fe;
        if (!efromstr(t, fe))
          throw bad_enum_value<ir::fundamental_e>(t);
        res.type.data = fe;
      }
    } else {
      // Несколько схем => anyOf из фундаментальных schema_ref-ов
      // Например: ["string", "null"] или ["integer", "string"]
      ir::anyof_schema anyof;
      for (auto& t : rtypes) {
        ir::fundamental_e fe;
        if (!efromstr(t, fe))
          throw bad_enum_value<ir::fundamental_e>(t);
        anyof.schemas.push_back(make_fundamental_ref(fe));
      }
      res.type.data = std::move(anyof);
    }
  }

  if (!as_requirements) {
    // не учитывая not, так как not это всегда требование и не может быть представлением
    as_requirements = count_true(schema.anyOf, schema.allOf, schema.oneOf) > 1;
    // если указано более одного, непонятно что отвечает за представление, а что за требование
    // поэтому тип any, но написанные схемы используются для валидации
    if (as_requirements)
      res.type.data = ir::fundamental_e::any;
  }

  auto collect_result = [&](std::string_view fieldname, auto schema) {
    if (!as_requirements) {
      res.type.data = std::move(schema);
    } else {
      ir::schema s;
      s.spec = res.spec;
      // создаём что-то типа abc/anyOf. Под abc/not уже лежит сама схема из not (её указатель), поэтому
      // обёртка not - отдельный указатель, иначе требование молча ссылалось бы на схему без отрицания
      s.jsonptr = res.jsonptr / (fieldname == "not" ? "@not" : fieldname);
      s.name = std::nullopt;
      s.type.data = std::move(schema);
      ir::schema_ref r = s.ref();
      if (!result_ir.schemas->insert(std::move(s)).second)
        throw validate_error("schema {}#{} already exists", r.spec, r.jsonptr->escaped());
      res.add_requirement(std::move(r));
    }
  };

  // ── логические комбинаторы ──────────────────────────────────────────────────

  auto resolve_array = [&](std::string_view ptr, const arrayof<reference_or<schema_object>>& from,
                           arrayof<ir::schema_ref>& to) {
    for (size_t i = 0; i < from.size(); ++i) {
      resolved_schema s = resolve_schema(from[i], file, jp / ptr / i, *this);
      to.push_back(s.ir_schema);
    }
  };

  if (schema.allOf) {
    ir::allof_schema t;
    resolve_array("allOf", *schema.allOf, t.schemas);
    collect_result("allOf", std::move(t));
  }
  if (schema.anyOf) {
    ir::anyof_schema t;
    resolve_array("anyOf", *schema.anyOf, t.schemas);
    if (schema.discriminator)
      t.discriminator = discriminator_lowering(*schema.discriminator, t.schemas);
    collect_result("anyOf", std::move(t));
  }
  if (schema.oneOf) {
    ir::oneof_schema t;
    resolve_array("oneOf", *schema.oneOf, t.schemas);
    if (schema.discriminator)
      t.discriminator = discriminator_lowering(*schema.discriminator, t.schemas);
    collect_result("oneOf", std::move(t));
  }
  if (schema.not_) {
    as_requirements = true;  // not всегда требоавние, никакого представления из него не сделать
    ir::not_schema t;
    t.type = resolve_schema(*schema.not_, file, jp / "not", *this).ir_schema;
    collect_result("not", std::move(t));
  }
  // ограничения всегда требование, на представление типа не влияют
  if (has_constraints(schema)) {
    ir::schema c;
    c.spec = res.spec;
    c.jsonptr = res.jsonptr / "constraints";
    c.name = std::nullopt;
    c.type.data = make_constraints(schema);
    res.add_requirement(c.ref());
    result_ir.schemas->insert(std::move(c));
  }
  return ret_schema();
}

// ожидает что file относительно spec dir
resolved_schema lowering_context::resolve_schema_ref(reference_object ref, fs::path file,
                                                     json_pointer refjp) {
  auto x = resolve_ref_to<schema_object>(std::move(ref), std::move(file), std::move(refjp), *this);
  auto& [schema, refered_file, jp] = x;
  // это не инлайн схема, так как мы смогли на неё сослаться, т.е. у неё было имя (хотя по коду подойдёт
  // любой json ptr, но это... такое). Если ссылались не на components/schemas, то варнинг уже был
  std::optional<std::string> name = try_get_name(jp);
  resolved_schema res;
  res.ir_schema = schema_lowering(schema, refered_file, std::move(jp), name).ref();
  res.front_schema = std::move(schema);
  return res;
}

// оборачивает визитор в ссылке с тем же интерфейсом,
// чтобы копирование результирующей структуры не ломало внутренние данные vtor (н-р если он не умеет
// копироваться) нужно из-за boost::pfr::for_each<***> которые копируют визитор
// структурой для удобного дебага
template <typename Vtor>
struct refvtor {
  Vtor vtor;

  decltype(auto) operator()(std::string_view name, auto&& arg) {
    return vtor(name, std::forward<decltype(arg)>(arg));
  };
};

// пылесосит объект в поисках всех схем, в том числе в других файлах на которые ссылаются $ref
struct schemas_collector {
 private:
  const fs::path& obj_file;
  lowering_context* ctx;
  json_pointer currentjp;

#define recursion (*this)

 public:
  explicit schemas_collector(const fs::path& obj_file, lowering_context& ctx)
      : obj_file(obj_file), ctx(&ctx) {
  }

#define SVARG_ADD_EXCEPTION_INFO(EXTYPE, INFO) \
  catch (std::exception & e) {                 \
    throw EXTYPE("{}/{}", INFO, e.what());     \
  }
  void dbgprint(std::string_view name) {
    // auto& ss = std::cout;
    // for (std::string_view s : currentjp.segments())
    //   ss << s << '.';
    // ss << name << '\n';
  }
  template <typename T>
  void operator()(std::string_view name, const arrayof<T>& f) try {
    dbgprint(name);
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.rm_last_segment();
    };
    for (size_t i = 0; i < f.size(); ++i)
      // не смотря на то что скоп может быть захвачен по string_view,
      // время жизни строки из format здесь должно быть валидно везде
      recursion(std::format("{}", i), f[i]);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name)

  template <typename... Types>
  void operator()(std::string_view name, const std::unordered_map<Types...>& f) try {
    dbgprint(name);
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.rm_last_segment();
    };
    for (auto& [key, v] : f)
      recursion(std::string_view(key), v);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name)

  template <typename... Types>
  void operator()(std::string_view name, const std::map<Types...>& f) try {
    dbgprint(name);
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.rm_last_segment();
    };
    for (auto& [key, v] : f)
      recursion(std::string_view(key), v);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name)

  template <typename T>
  void operator()(std::string_view name, const ordered_string_map<T>& m) try {
    dbgprint(name);
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.rm_last_segment();
    };
    for (auto& [key, v] : m)
      recursion(std::string_view(key), v.value);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name)

  void operator()(std::string_view name, const uint64_t& f) {
    dbgprint(name);
  }
  void operator()(std::string_view name, const int64_t& f) {
    dbgprint(name);
  }
  void operator()(std::string_view name, const number& f) {
    dbgprint(name);
  }
  void operator()(std::string_view name, const bool& f) {
    dbgprint(name);
  }
  void operator()(std::string_view name, const Any& f) {
    dbgprint(name);
  }
  void operator()(std::string_view name, const string& f) {
    dbgprint(name);
  }
  void operator()(std::string_view name, const parameter_location& f) {
    dbgprint(name);
  }
  // обработано специально, так как ключ _responses отсутствует в оригинальном json
  void operator()(std::string_view name, const responses_object& rsps) {
    dbgprint(name);
    assert(!is_keyfield(name));
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.rm_last_segment();
    };
    recursion("default", rsps.default_);
    for (auto& [key, v] : rsps._responses)
      recursion(std::string_view(key), v);
  }
  void operator()(std::string_view name, const frontend_type auto& x) try {
    assert(!is_keyfield(name));  // должен был обработать специально
    name = unescape_field_name(name);
    currentjp.add_key(name);
    dbgprint(name);
    on_scope_exit {
      currentjp.rm_last_segment();
    };
    for_each_field_with_name(x, refvtor(*this));
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  void operator()(std::string_view name, const schema_object& x) try {
    dbgprint(name);
    // схемы могут встретиться в:
    // * parameter_object/header_object/media_type_object - там они инлайн
    // * components_object (components/schemas) - там они не инлайн
    // * в самих schema_object - их вообще этот визитор не посещает, это обрабатывается в schema_lowering
    std::optional<std::string> schema_name = try_get_name(currentjp / name);
    ctx->schema_lowering(x, obj_file, currentjp / name, schema_name);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  template <typename T>
  void operator()(std::string_view name, const reference_or<T>& x) {
    dbgprint(name);

    if (auto* r = x.get_if_ref()) {
      if constexpr (!std::is_same_v<T, schema_object>) {
        // проходимся на зарезолвленном объекте, чтобы собрать всё что упомянуто там
        auto [resolved, path, jp] = resolve_ref_and_ret(x, obj_file, currentjp / name, *ctx);
        schemas_collector inner(path, *ctx);
        inner.currentjp = std::move(jp);
        std::string lastseg = inner.currentjp.last_segment();
        if (!inner.currentjp.empty())
          inner.currentjp.rm_last_segment();
        // при "переезде" в другой файл name теряет значение
        refvtor{inner}(lastseg, resolved);
        return;
      } else {
        // вызывается чтобы обойти всё и собрать упоминающиеся ссылки
        ctx->resolve_schema_ref(*r, obj_file, currentjp / name);
      }
    } else {
      recursion(name, *x.get_if_value());
    }
  }

  template <typename T>
  void operator()(std::string_view name, const optional<T>& x) {
    if (x)
      recursion(name, *x);
  }
  template <typename T>
  void operator()(std::string_view name, const std::optional<T>& x) {
    if (x)
      recursion(name, *x);
  }
};

static void visit_path_item_callbacks(const path_item_object& item, auto&& visitor, fs::path file,
                                      json_pointer jp) {
  for (auto&& [methodstr, op] : item.all_operations_with_method()) {
    if (op.callbacks) {
      for (auto& [name, cb] : *op.callbacks)
        visitor(name, cb, file, jp / methodstr / "callbacks" / name);
    }
  }
}

// посещает все калбеки и передаёт визитору имя + reference_or<callback> + json ptr где он лежит
// Note: может быть много калбеков с одинаковым именем. Они отличаются по методу и тд
static void visit_all_callbacks(const openapi_object& api, fs::path file, lowering_context& ctx,
                                auto&& visitor) {
  // components/callbacks
  if (api.components && api.components->callbacks) {
    for (auto& [name, cb] : *api.components->callbacks)
      visitor(name, cb, file, "/components/callbacks"_jp / name);
  }

  // components/pathItems
  if (api.components && api.components->pathItems) {
    for (auto& [name, item] : *api.components->pathItems) {
      auto [ritem, reffile, refjp] = resolve_ref_and_ret(item, file, "/components/pathItems"_jp / name, ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjp));
    }
  }

  // paths -> operations -> callbacks
  if (api.paths) {
    for (auto& [path, item] : *api.paths) {
      auto [ritem, reffile, refjp] = resolve_ref_and_ret(item.value, file, "/paths"_jp / path, ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjp));
    }
  }

  // webhooks -> operations -> callbacks
  if (api.webhooks) {
    for (auto& [name, item] : *api.webhooks) {
      auto [ritem, reffile, refjp] = resolve_ref_and_ret(item, file, "/webhooks"_jp / name, ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjp));
    }
  }
}
// TODO?  если вообще все ссылки ремапить сразу, то как будто легче код становится сразу
// и ещё можно сразу вычислить для всех json pointer и сложить где-то либо в json либо в структуры
// распаршенные
// TODO в интерфейс принимать сразу все файлы которые нужно сгенерировать и объединить процесс
// загрузки всех файлов, предобработки (и там ремап ссылок абсолютно всех даже в файлах НА которые ссылки)
// и затем generate, в итоге один метод есть просто

ir::body lowering_context::media_type_object_lowering(const media_type_object& obj, fs::path file,
                                                      json_pointer jp, std::string_view content_type,
                                                      bool is_required_body) {
  ir::body result;
  // возможно media type range
  result.contentType = content_type;
  result.required = is_required_body;
  // поток элементов (OpenAPI 3.2) не поддерживается: бинарные тела и так передаются потоком, а схема
  // элемента для генерации не нужна
  if (obj.itemSchema)
    emit_warn(file, jp, "'itemSchema' is not supported and ignored");
  if (obj.schema) {
    auto [ref, rschema] = resolve_schema(*obj.schema, file, jp / "schema", *this);
    result.schema = ref;
    if (obj.encoding) {
      if (media_type_supports_encodings(content_type)) {
        auto _ = guard_array(result.membersEncoding);
        // есть только если есть schema (валидируется до этого)
        // наличие property в схеме не проверяется, так как там очень сложно - может быть в anyOf и тд
        // где-то
        for (auto& [property, enc] : *obj.encoding) {
          if (enc.contentType)
            result.membersEncoding->try_emplace(property, *enc.contentType);
          // хедеры частей есть только у multipart. Content-Type описывается contentType, а
          // Content-Disposition (form-data) формируется из имени свойства, поэтому игнорируются
          if (enc.headers && !enc.headers->empty() && lowered(content_type).starts_with("multipart/")) {
            std::set<ir::header> headers;
            for (auto& [name, hdr] : *enc.headers) {
              std::string lname = lowered(name);
              if (lname == "content-type" || lname == "content-disposition")
                continue;
              headers.insert(header_lowering(name, hdr, file, jp / "encoding" / property / "headers" / name));
            }
            if (!headers.empty()) {
              if (!result.membersHeaders)
                result.membersHeaders.emplace();
              result.membersHeaders->try_emplace(property, std::move(headers));
            }
          }
          // остальное пока игнорируется (style, explode...)
        }
      } else {
        emit_warn(file, jp, "media type `{}` does not support encodings, `encoding` ignored", content_type);
      }
    }
  } else {
    // без схемы содержимое любое (так в 3.1 описываются бинарные тела: application/octet-stream: {})
    result.schema = ir::make_fundamental_ref(ir::fundamental_e::any);
  }
  return result;
}

ir::header lowering_context::header_lowering(std::string_view name, const reference_or<header_object>& hdr,
                                             fs::path file, json_pointer jp) {
  ir::header result_hdr;
  result_hdr.name = lowered(name);
  auto [hdrobj, reffile, refjp] = resolve_ref_and_ret(hdr, file, jp, *this);
  if (hdrobj.required)
    result_hdr.required = *hdrobj.required;
  if (hdrobj.schema && hdrobj.content)
    throw validate_error("header object cannot have both `schema` and `content` set");
  if (hdrobj.schema) {
    resolved_schema rs = resolve_schema(*hdrobj.schema, reffile, refjp / "schema", *this);
    result_hdr.schema = rs.ir_schema;
    result_hdr.default_value = rs.front_schema.default_;
  }
  if (hdrobj.content) {
    if (hdrobj.content->size() != 1)
      throw validate_error("header object 'content' MUST contain one entry, but contains `{}`",
                           hdrobj.content->size());
    auto& [content_type, mtc] = *hdrobj.content->begin();
    if (mtc.schema)
      result_hdr.schema =
          resolve_schema(*mtc.schema, reffile, refjp / "content" / content_type / "schema", *this).ir_schema;
    result_hdr.encoding = content_type;
  }
  if (result_hdr.required.value_or(false) && result_hdr.default_value) {
    emit_warn(file, jp, "required header `{}` has default, default ignored", name);
    result_hdr.default_value.reset();
  }
  result_hdr.validate();
  return result_hdr;
}

// statusstr пустой для дефолтного респонса
ir::response_desc lowering_context::response_lowering(std::optional<std::string_view> statusstr,
                                                      const response_object& rsp, fs::path file,
                                                      json_pointer jp) {
  ir::response_desc result;
  if (statusstr)
    result.set_status(*statusstr);
  else
    result.make_default();
  if (rsp.headers) {
    auto _ = guard_array(result.headers);
    for (auto& [name, hdr] : *rsp.headers)
      result.headers->insert(header_lowering(name, hdr, file, jp / "headers" / name));
  }
  if (rsp.content) {
    auto _ = guard_array(result.bodies);
    for (auto& [content_type_, bodydesc] : *rsp.content) {
      ir::content_type_t content_type = content_type_;
      // для респонса все body требуемые, так как если указан тип контента и статус код, дальше указано тело,
      // то ожидается что оно придёт
      result.bodies->insert(media_type_object_lowering(bodydesc, file, jp / "content" / content_type_,
                                                       content_type.str(), /*is_required_body=*/true));
    }
  }

  return result;
}

ir::operation_parameter lowering_context::parameter_lowering(const parameter_object& param, fs::path file,
                                                             json_pointer jp, std::string_view path) {
  ir::operation_parameter result;
  result.location = param.in;
  // имена хедеров не зависят от регистра, а HTTP/2 требует нижний (как у хедеров ответов)
  result.name = param.in == parameter_location::header ? lowered(param.name) : param.name;
  if (param.schema) {
    resolved_schema rs = resolve_schema(*param.schema, file, jp / "schema", *this);
    result.schema = rs.ir_schema;
    result.default_value = rs.front_schema.default_;
  } else {
    // валидация была
    assert(param.content && param.content->size() == 1);
    // content type
    auto& [content_type_, mediaobj] = *param.content->begin();
    result.encoding = content_type_;
    if (!mediaobj.schema)
      throw validate_error("parameter schema not setted. Both `schema` and `content/schema` not setted");
    result.schema =
        resolve_schema(*mediaobj.schema, file, jp / "content" / content_type_ / "schema", *this).ir_schema;
  }
  if (param.in == parameter_location::path) {
    // валидация уже была
    assert(!(param.required && !*param.required));
    if (!path.contains(std::string("{") + param.name + std::string("}")))
      throw validate_error("path `{}` does not contain path parameter with name", path, param.name);
    result.required = true;
  }
  if (param.required)
    result.required = param.required;
  // style/explode - как есть в meta, что из этого поддерживать, решает бэкенд
  if (param.style || param.explode) {
    if (result.encoding) {
      emit_warn(file, jp, "parameter `{}` with `content` ignores style/explode", param.name);
    } else {
      result.meta.emplace();
      if (param.style)
        (*result.meta)[std::string(x_param_style)] = Any{boost::json::value(std::string_view(*param.style))};
      if (param.explode)
        (*result.meta)[std::string(x_param_explode)] = Any{boost::json::value(bool(*param.explode))};
    }
  }
  if (result.required.value_or(false) && result.default_value) {
    // default имеет смысл только для необязательного параметра
    emit_warn(file, jp, "required parameter `{}` has default, default ignored", param.name);
    result.default_value.reset();
  }
  // TODO result.meta
  return result;
}
std::set<ir::body> lowering_context::request_body_lowering(request_body_object obj, fs::path file,
                                                           json_pointer jp) {
  std::set<ir::body> result;
  for (auto& [content_type_, mediaobj] : obj.content) {
    ir::content_type_t content_type = content_type_;
    ir::body b =
        media_type_object_lowering(mediaobj, file, jp / "content" / content_type_, content_type.str(),
                                   /*is_required_body=*/obj.required.value_or(false));
    result.insert(std::move(b));
  }
  return result;
}

static std::optional<ir::operation_parameter> auth_parameter(const security_scheme_object& sec) {
  auto make_string_param = [](std::string_view name, parameter_location in = parameter_location::header) {
    ir::operation_parameter param;
    // хедер - в нижнем регистре, как и остальные хедеры
    param.name = in == parameter_location::header ? lowered(name) : std::string(name);
    param.location = in;
    param.required = true;  // позже станет optional если есть другие параметры
    param.schema = ir::make_fundamental_ref(ir::fundamental_e::string);
    param.auth_related = true;
    return param;
  };
  constexpr std::string_view authhdr = "Authorization";
  if (sec.type == "apiKey") {
    assert(sec.in && sec.name);  // validate должен был проверить
    parameter_location in;
    if (!efromstr(*sec.in, in))
      throw validate_error("cannot parse parameter location from `{}`", *sec.in);
    return make_string_param(*sec.name, in);
  } else if (sec.type == "mutualTLS") {
    return std::nullopt;  // ничего не происходит на уровне апи, только на уровне tls
  } else if (sec.type == "http") {
    return make_string_param(authhdr);
  } else if (sec.type == "oauth2") {
    assert(sec.flows);
    return make_string_param(authhdr);
  } else if (sec.type == "openIdConnect") {
    assert(sec.openIdConnectUrl);
    return make_string_param(authhdr);
  } else {
    throw validate_error("unknown auth type `{}`", sec.type);
  }
}

static std::vector<ir::operation_parameter> make_auth_parameters(
    const std::vector<std::vector<security_scheme_object>>& requirement_groups) {
  // несколько групп авторизаций с ||, значит каждая из них становится опциональной
  bool is_optional = requirement_groups.size() > 1;
  std::vector<ir::operation_parameter> result;
  for (auto& group : requirement_groups) {
    for (const security_scheme_object& sec : group) {
      std::optional p = auth_parameter(sec);
      // если такого name + location ещё нет
      if (p && !std::ranges::contains(result, *p)) {
        result.push_back(std::move(*p));
        result.back().required = !is_optional;
      }
    }
  }
  return result;
}

std::vector<std::vector<security_scheme_object>> lowering_context::resolve_security(
    fs::path file, const openapi_object& api, const operation_object& op, meta_t& operation_meta) {
  // внутри security_requirement_object это && для требований, а в массивах security это  ||
  using span_t = std::span<const security_requirement_object>;
  span_t requirements;
  if (op.security) {
    requirements = *op.security;
  } else if (api.security) {
    requirements = *api.security;
  } else
    return {};
  std::vector<std::vector<security_scheme_object>> result;

  for (auto&& sec : requirements) {
    // пустые требования (security: {}) не должны пропускаться
    std::vector<security_scheme_object> group;
    for (auto& [name, scopes] : sec) {
      auto& schemas = api.components->securitySchemes;
      if (!schemas || !schemas->contains(name))
        throw validate_error("unknown security scheme `{}`", name);
      auto [obj, path, refjp] =
          resolve_ref_and_ret(schemas->at(name), file, "/components"_jp / "securitySchemes" / name, *this);
      if (obj.type != "oauth2" && obj.type != "openIdConnect" && !scopes.empty())
        throw validate_error("scopes must be empty for this auth type (`{}`)", obj.type);

      if (obj.type == "http") {
        if (obj.bearerFormat)
          operation_meta[std::string(x_bearer_format)] = Any(boost::json::value(*obj.bearerFormat));
      } else if (obj.type == "oauth2") {
        assert(obj.flows);
        operation_meta[std::string(x_auth_flows)] = Any(to_json(*obj.flows));
        operation_meta[std::string(x_auth_scopes_oauth2)] = Any(to_json(scopes));
      } else if (obj.type == "openIdConnect") {
        assert(obj.openIdConnectUrl);  // должно быть проверено в validate
        operation_meta[std::string(x_openid_connect_url)] = Any(boost::json::value(*obj.openIdConnectUrl));
        operation_meta[std::string(x_auth_scopes_openIdConnect)] = Any(to_json(scopes));
      }
      group.push_back(std::move(obj));
    }
    result.push_back(std::move(group));
  }
  return result;
}

string_map<ir::operation> lowering_context::path_item_object_lowering(const openapi_object& api,
                                                                      std::string_view path, fs::path spec,
                                                                      fs::path resolve_ref_file,
                                                                      json_pointer jp, std::string_view name,
                                                                      const path_item_object& item) {
  assert(parsable_as<path_item_object>(jp.access(load_as_json(spec))) && "no such path item");
  string_map<ir::operation> result;
  std::vector<ir::operation_parameter> common_params;
  if (item.parameters) {
    size_t i = 0;
    for (auto& param : *item.parameters) {
      auto [rparam, reffile, refjp] =
          resolve_ref_and_ret(param, resolve_ref_file, jp / "parameters" / i, *this);
      ir::operation_parameter p = parameter_lowering(rparam, std::move(reffile), std::move(refjp), path);
      common_params.push_back(std::move(p));
      ++i;
    }
  }

  for (auto [methodstr, o] : item.all_operations_with_method()) {
    ir::operation op;
    if (!o.SVARG_META_KEYFIELD.empty())
      op.meta = o.SVARG_META_KEYFIELD;
    // не закрепляем вебхуки за серверами
    if (!path.empty()) {
      auto srvs = [&] {
        if (o.servers && !o.servers->empty())
          return *o.servers;
        if (item.servers && !item.servers->empty())
          return *item.servers;
        assert(api.servers && !api.servers->empty());  // должен быть хотя бы один дефолтный '/'
        return *api.servers;
      }();
      op.servers = std::set(srvs.begin(), srvs.end());
    }
    op.spec = spec.filename().replace_extension().generic_string();
    if (o.operationId)
      op.id = *o.operationId;
    else {
      if (name.empty())
        throw validate_error("path item name cannot be empty");
      op.id = std::format("{}{}{}", methodstr, to_upper_ascii(name[0]), name.substr(1));
    }
    op.method = uppered(methodstr);
    op.path = path;
    if (o.responses) {
      if (o.responses->default_) {
        auto [obj, reffile, refjp] = resolve_ref_and_ret(*o.responses->default_, resolve_ref_file,
                                                         jp / methodstr / "responses" / "default", *this);
        op.defaultResponse = response_lowering(std::nullopt, obj, std::move(reffile), std::move(refjp));
      }
      auto _ = guard_array(op.responses);
      for (auto&& [status, r] : o.responses->_responses) {
        auto [obj, reffile, refjp] =
            resolve_ref_and_ret(r, resolve_ref_file, jp / methodstr / "responses" / status, *this);
        ir::response_desc rsp = response_lowering(status, obj, std::move(reffile), std::move(refjp));
        op.responses->insert(std::move(rsp));
      }
    }
    if (o.parameters) {
      auto _ = guard_array(op.parameters);
      size_t i = 0;
      for (auto& param : *o.parameters) {
        auto [rparam, reffile, refjp] =
            resolve_ref_and_ret(param, resolve_ref_file, jp / methodstr / "parameters" / i, *this);
        ir::operation_parameter p = parameter_lowering(rparam, std::move(reffile), std::move(refjp), path);
        op.parameters->push_back(std::move(p));
        ++i;
      }
    }
    if (o.requestBody) {
      auto _ = guard_array(op.bodies);
      auto [rbody, reffile, refjp] =
          resolve_ref_and_ret(*o.requestBody, resolve_ref_file, jp / methodstr / "requestBody", *this);
      op.bodies = request_body_lowering(rbody, std::move(reffile), std::move(refjp));
    }
    {
      auto _ = guard_array(op.parameters);
      for (const auto& p : common_params) {
        if (!op.has_parameter(p.name, p.location))
          op.parameters->push_back(p);
      }
    }
    {
      auto _g = guard_array(op.parameters);
      auto _ = guard_array(op.meta);
      std::vector op_sec = resolve_security(path, api, o, *op.meta);
      for (auto& ap : make_auth_parameters(op_sec)) {
        assert(ap.is_auth_related());
        if (auto* pp = op.find_parameter(ap.name, ap.location)) {
          if (!pp->is_auth_related()) {
            throw validate_error(
                "conflicting parameters: one named `{}` in `{}` and auth related param named `{}` in `{}`",
                pp->name, e2str(pp->location), ap.name, e2str(ap.location));
          }
        } else {
          op.parameters->push_back(std::move(ap));
        }
      }
    }
    // не зависит от порядка если чё
    result.try_emplace(op.method, std::move(op));
  }

  return result;
}

void lowering_context::visit_and_generate_ir(const openapi_object& api, const fs::path& obj_file) {
  auto& result = this->result_ir;
  {
    auto _1 = guard_array(result.schemas);
    auto _2 = guard_array(result.operations);
    auto _3 = guard_array(result.webhooks);

    // собираем все схемы, в том числе из калбеков, вебхуков и paths
    schemas_collector collector(obj_file, *this);
    for_each_field_with_name(api, refvtor(collector));

    visit_all_callbacks(api, obj_file, *this,
                        [&](std::string_view name, const reference_or<callback_object>& cb, fs::path cbpath_,
                            json_pointer jp) {
                          assert(parsable_as<callback_object>(jp.access(load_as_json(cbpath_))));
                          auto [rcb, cbpath, cbjp] = resolve_ref_and_ret(cb, cbpath_, jp, *this);
                          // TODO not ignore `expr` (складывать в список для дальнейшей генерации)
                          for (auto&& [expr, pathItem] : rcb) {
                            // используем cbpath, т.к. ссылки могут указывать туда если он загружен оттуда
                            auto [rpi, rpipath, rpijp] =
                                resolve_ref_and_ret(pathItem, cbpath, cbjp / expr, *this);
                            // используется путь откуда операция взялась, но она всё ещё декларирована в
                            // obj_file и является операцией этого апи
                            auto ops = path_item_object_lowering(api,
                                                                 /*path=*/"", obj_file, rpipath,
                                                                 std::move(rpijp), name, std::move(rpi));
                            for (auto& [method, op] : ops)
                              result.webhooks->insert(std::move(op));
                          }
                        });

    if (api.paths) {
      // если были pathItems и на них ссылались и в paths и в webhooks, то они продублируются на уровне IR
      // но это нормально
      for (auto& [path, item] : *api.paths) {
        auto [ritem, file, item_jp] = resolve_ref_and_ret(item.value, obj_file, "/paths"_jp / path, *this);

        // отключено ради упрощения генерации и предсказуемости имён
        //
        // string_map<std::string> name2path;
        // string_map<std::string> path2name;
        // std::string generatedname = path_to_name(path, name2path, path2name);
        if (!validate_maybe_templated_path_parameters(path))
          throw validate_error("invalid path `{}`, do not passes validation", path);
        std::string generatedname(cxx::to_cxx_ident(path));
        auto ops = path_item_object_lowering(api, path, obj_file, file, std::move(item_jp), generatedname,
                                             std::move(ritem));
        for (auto& [method, op] : ops) {
          // у многих операций одного path с разными методами может быть один и тот же приоритет - это
          // нормально, так приоритет используется для выбора хендлера, а метод разделяет их
          // приоритет больше у тех, кто раньше в файле спецификации написан
          op.priority = item.insertion_index;
          // /a/{x} и /a/{y} с одним методом - одна операция (path-templating в OpenAPI), молча выкинуть
          // вторую нельзя
          // при неудаче узел не создаётся и `op` не перемещён
          auto [it, inserted] = result.operations->insert(op);
          if (!inserted)
            throw validate_error("operation `{}` ({} {}) conflicts with `{}` ({} {}): same method and path",
                                 op.id, op.method, op.path, it->id, it->method, it->path);
        }
      }
    }
  }
}

struct arrays_sorter : ir::ir_visitor_base {
  using ir::ir_visitor_base::operator();

  template <typename T>
  void operator()(const arrayof<T>& obj) {
    // массивы не влияют на хеш/сравнения в IR, const_cast безопасен
    auto& arr = const_cast<arrayof<T>&>(obj);
    std::sort(arr.begin(), arr.end());
  }
};

ir::ir_object lowering_context::generate_specs(fs::path specsdir, std::vector<fs::path> specs2generate,
                                               openapi_options options) {
  // подготовка
  if (!fs::exists(specsdir) || !fs::is_directory(specsdir))
    throw parse_error("specs directory not exist, provided path: {}", specsdir.generic_string());
  opts = options;
  specs_dir = fs::canonical(specsdir);
  jcache.set_preprocessor([this](const fs::path& spec, boost::json::value v) {
    return resolve_raw_references(std::move(v), specs_dir / spec, opts);
  });

  std::map<fs::path, openapi_object> specs;

  // парсинг спек

  for (fs::path& s : specs2generate) {
    s = find_existing_file(std::move(s), specs_dir);
    if (!specs.contains(s))
      specs[s] = parse_spec(jcache.load(specs_dir / s), opts);
  }

  // перевод в IR

  for (auto& [p, _] : specs) {
    try {
      // результат собирается в this->result_ir
      visit_and_generate_ir(specs.at(p), p);
    } catch (std::exception& e) {
      throw parse_error("{}/{}", p.generic_string(), e.what());
    }
  }
  // сортируем все массивы, чтобы получать всегда одинаковый IR
  // (порядок заполнения мог зависеть от реализации)
  arrays_sorter{}.visit(result_ir);
  return std::move(result_ir);
}

}  // namespace svarg
