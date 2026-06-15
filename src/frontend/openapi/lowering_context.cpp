#include "svarg/frontend/openapi/lowering_context.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/merge_with_override.hpp"
#include "svarg/utils/json_pointer.hpp"
#include "svarg/utils/guard_array.hpp"
#include "svarg/utils/for_each_field.hpp"

#include <boost/pfr.hpp>

namespace svarg {

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
// TODO cache + возвращать вместо инвокабл
// TODO тест на путь до инлайн схемы через ссылку с оверрайд полем (должно продолжаться "вглубь")
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
      ctx.emit_warn(file, chain.jp.empty() ? parse_ref(ref.ref_).json_ptr.escaped() : chain.jp.escaped(),
                    "$ref should be into /components/*, but $ref = {}", ref.ref_);
    }
    boost::json::value json = chain.follow(std::move(ref));
    auto refered = from_json<reference_or<T>>(json);
    if (auto* value = std::get_if<T>(&refered)) {
      if (chain.first_with_overrides) {
        auto [reffile, refjp] = *std::move(chain.first_with_overrides);
        // нужно изменить само загруженное апи, чтобы json pointer на поле внутри $reef
        // с перекрытым полем стал валиден (т.к. он указывает внутрь $ref)
        ctx.replace_in_cache(reffile, refjp, std::move(json));
        return {std::move(*value), std::move(reffile), std::move(refjp)};
      } else
        return {std::move(*value), std::move(chain.file), std::move(chain.jp)};
    }
    ref = std::get<reference_object>(std::move(refered));
  }
  throw semantic_error("recursion depth max reached");
}

template <typename T>
object_and_path<T> resolve_ref_and_ret(reference_or<T> x, fs::path file, json_pointer jp,
                                       lowering_context& ctx) {
  if (auto* r = std::get_if<reference_object>(&x))
    return resolve_ref_to<T>(std::move(*r), std::move(file), std::move(jp), ctx);
  else
    return object_and_path(std::move(std::get<T>(x)), std::move(file), std::move(jp));
}

// jp используется только если это инлайн схема для создания уникального scope
[[nodiscard]] static resolved_schema resolve_schema(const reference_or<schema_object>& sor,
                                                    const fs::path& file, json_pointer jp,
                                                    lowering_context& ctx) {
  if (auto* ref = std::get_if<reference_object>(&sor)) {
    return ctx.resolve_schema_ref(*ref, file, std::move(jp));
  } else {
    auto s = ctx.schema_lowering(std::get<schema_object>(sor), file, std::move(jp), std::nullopt);
    return {std::move(s).ref(), std::get<schema_object>(sor)};
  }
}

ir::object_schema lowering_context::make_object_type(schema_object const& schema, const fs::path& file,
                                                     json_pointer jp) {
  ir::object_schema ot;
  if (schema.properties) {
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      ir::object_field_info fi;
      fi.name = prop_name;
      auto [schemaref, field_schema] = resolve_schema(prop_sor, file, jp / "properties" / prop_name, *this);
      fi.type = schemaref;
      if (field_schema.nullable || field_schema.default_)
        fi.required = false;
      else if (schema.required) {
        if (field_schema.nullable || field_schema.default_) {
          emit_warn(file, jp.unescaped(), "property {}` marked as required, but {}. `required` ignored",
                    prop_name, field_schema.nullable ? "has nullable type" : "has default");
        } else if (field_schema.is_write_only() || field_schema.is_read_only()) {
          // readOnly/writeOnly означает, что по сути есть два типа - для запроса и ответа и required зависит
          // от этого здесь генерируется только один тип, поэтому оба readOnly/writeOnly означают по факту
          // опциональность поля
          emit_warn(file, jp.unescaped(), "property {} marked as {}, `required` ignored", prop_name,
                    field_schema.is_write_only() ? "writeOnly" : "readOnly");
        } else {
          for (string const& x : *schema.required) {
            if (prop_name == x)
              fi.required = true;
          }
        }
      }
      ot.fields.push_back(std::move(fi));
    }
  }
  if (schema.additionalProperties) {
    ot.additional_fields =
        resolve_schema(*schema.additionalProperties, file, jp / "additionalProperties", *this).ir_schema;
  }
  return ot;
}

ir::discriminator_desc lowering_context::discriminator_lowering(const discriminator_object& d,
                                                                const array<ir::schema_ref>& schemas) {
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

/*
- добавляет встреченные схемы в `schemas`. Как минимум один из schema + инлайн схемы
- file - путь до текущего файла (относительно директории со спеками)
- если name нет, то схема inline. Если есть, то на схему можно сослаться
Логика работы:
 * если схема true/false - остальные поля игнорируются (их не должно быть)
 * если есть type - он отвечает за представление типа,
   а возможные anyOf/oneOf/allOf/not/enum/const описывают требования
 * если нет type и есть const/enum/allOf/oneOf/allOf/not - то это они описывают представление.
 Если есть одновременно два и более из них - варнинг и все они будут требованиями, а представление будет Any
 * если ничего нет, но есть properties - то как будто неявно type: object
 * если нет ничего, то схема any, но возможно всё ещё с валидацией (н-р required [a, b, c])
*/
schema_or_ref lowering_context::schema_lowering(const schema_object& schema, const fs::path& file,
                                                json_pointer jp, std::optional<std::string_view> name) {
  ir::schema res;
  res.spec = file.lexically_normal().generic_string();
  res.jsonptr = jp;
  auto ret_res = [&] {
    // TODO такие проверки добавить и для операций и тд + нужно ещё проверять что оно парсится как схема
    // TODO serialize/parse и для json pointer надо (escape/unescape)
    assert(jp.access(load_as_json(res.spec)) && "invalidly generated scheme");
    ir.schemas.insert(res);
    return schema_or_ref(std::move(res));
  };
  if (name)
    res.name = *name;
  else
    res.name = std::nullopt;
  if (auto it = ir.schemas.find(res); it != ir.schemas.end())
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
    res.data = ir::any_schema{};
    return ret_res();
  }
  if (schema.allow_never()) {
    res.data = ir::never_schema{};
    return ret_res();
  }
  res.data = ir::any_schema{};  // позже заменится (или нет)

  // если нужно anyOf/... парсить как требования, а не как представление типа
  bool as_requirements = schema.enum_ || schema.type;

  // ── enum_ / const ─────────────────────────────────────────────────────────
  assert(!schema.const_);                        // postparse должен был удалить
  assert(!(!schema.type && schema.properties));  // postparse должен был обработать

  if (schema.enum_) {
    if (schema.enum_->empty())
      throw semantic_error("enum at `{}` must have at least one value", jp.view());
    ir::enum_schema es{.values = *schema.enum_};
    if (schema.type) {
      // как будто ссылка на самого себя, на самом деле ссылаемся на тип репрезентации
      // из этой схемы
      es.type = res.ref();
    }
    res.data = std::move(es);
  } else if (schema.type) {
    array<string> rtypes = schema.type->values;

    std::sort(rtypes.begin(), rtypes.end());
    rtypes.erase(std::unique(rtypes.begin(), rtypes.end()), rtypes.end());

    if (rtypes.size() == 1) {
      // Единственный тип — прямо в fundamental или object_type
      const auto& t = rtypes[0];

      if (t == "object") {
        res.data = make_object_type(schema, file, jp);
      } else if (t == "array") {
        ir::array_schema at;
        if (!schema.items)
          throw parse_error("schema with type 'array' must contain 'items'");
        at.element = resolve_schema(*schema.items, file, jp / "items", *this).ir_schema;
        res.data = std::move(at);
      } else {
        // Всё остальное — фундаментальный тип
        ir::fundamental_e fe;
        efromstr(t, fe);
        res.data = fe;
      }
    } else {
      // Несколько схем → anyOf из фундаментальных schema_ref-ов
      // Например: ["string", "null"] или ["integer", "string"]
      ir::anyof_schema anyof;
      for (auto& t : rtypes) {
        ir::fundamental_e fe;
        efromstr(t, fe);
        if (fe == ir::fundamental_e::object || fe == ir::fundamental_e::array) {
          throw semantic_error(
              "field type with several types, where one of them object or array is not supported");
        }
        anyof.schemas.push_back(make_fundamental_ref(fe));
      }
      res.data = std::move(anyof);
    }
  }

  if (!as_requirements) {
    // TODO warning
    as_requirements = count_true(schema.anyOf, schema.allOf, schema.oneOf, schema.not_) > 1;
    // если указано более одного, непонятно что отвечает за представление, а что за требование
    // поэтому тип any, но написанные схемы используются для валидации
    if (as_requirements)
      res.data = ir::any_schema{};
  }

  auto collect_result = [&](std::string_view fieldname, auto schema) {
    if (!as_requirements) {
      res.data = std::move(schema);
    } else {
      ir::schema s;
      // создаём что-то типа file#abc/anyOf
      s.jsonptr = res.jsonptr / fieldname;
      s.name = std::nullopt;
      s.data = std::move(schema);
      ir::schema_ref r = s.ref();
      ir.schemas.insert(std::move(s));
      res.add_requirement(std::move(r));
    }
  };

  // ── логические комбинаторы ──────────────────────────────────────────────────

  auto resolve_array = [&](std::string_view ptr, const array<reference_or<schema_object>>& from,
                           array<ir::schema_ref>& to) {
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
    ir::not_schema t;
    t.type = resolve_schema(*schema.not_, file, jp / "not", *this).ir_schema;
    collect_result("not", std::move(t));
  }
  return ret_res();
}

// ожидает что file относительно spec dir
resolved_schema lowering_context::resolve_schema_ref(reference_object ref, fs::path file,
                                                     json_pointer refjp) {
  auto [schema, refered_file, jp] =
      resolve_ref_to<schema_object>(std::move(ref), std::move(file), std::move(refjp), *this);
  // это не инлайн схема, так как мы смогли на неё сослаться, т.е. у неё было имя (хотя по коду подойдёт
  // любой json ptr, но это... такое). Если ссылались не на components/schemas, то варнинг уже был
  std::string name = jp.last_segment();
  if (name.empty())
    throw semantic_error("name of schema cannot be empty. json pointer: {}", jp.view());
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
 public:  // TODO private
  const fs::path& obj_file;
  lowering_context* ctx;
  std::unordered_set<ir::schema>& schemas;
  json_pointer currentjp;

#define recursion (*this)

 public:
  explicit schemas_collector(const fs::path& obj_file, std::unordered_set<ir::schema>& schemas,
                             lowering_context& ctx)
      : obj_file(obj_file), ctx(&ctx), schemas(schemas) {
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
  void operator()(std::string_view name, const array<T>& f) try {
    dbgprint(name);
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.remove_scope();
    };
    for (size_t i = 0; i < f.size(); ++i)
      // не смотря на то что скоп может быть захвачен по string_view,
      // время жизни строки из format здесь должно быть валидно везде
      recursion(std::format("{}", i), f[i]);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name)

  template <typename T>
  void operator()(std::string_view name, const string_map<T>& f) try {
    dbgprint(name);
    currentjp.add_key(name);
    on_scope_exit {
      currentjp.remove_scope();
    };
    for (auto& [key, v] : f)
      recursion(std::string_view(key), v);
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
      currentjp.remove_scope();
    };
    recursion("default", rsps.default_);
    for (auto& [key, v] : rsps._responses)
      recursion(std::string_view(key), v);
  }
  void operator()(std::string_view name, const aggregate auto& x) try {
    assert(!is_keyfield(name));  // должен был обработать специально
    name = unescape_field_name(name);
    currentjp.add_key(name);
    dbgprint(name);
    on_scope_exit {
      currentjp.remove_scope();
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
    std::optional<std::string_view> schema_name;
    if (currentjp.view().ends_with("/components/schemas"))
      schema_name = name;
    ctx->schema_lowering(x, obj_file, currentjp / name, schema_name);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  template <typename T>
  void operator()(std::string_view name, const reference_or<T>& x) {
    dbgprint(name);

    if (auto* r = std::get_if<reference_object>(&x)) {
      if constexpr (!std::is_same_v<T, schema_object>) {
        // проходимся на зарезолвленном объекте, чтобы собрать всё что упомянуто там
        auto [resolved, path, jp] = resolve_ref_and_ret(x, obj_file, currentjp / name, *ctx);
        schemas_collector inner(path, schemas, *ctx);
        inner.currentjp = std::move(jp);
        std::string lastseg = inner.currentjp.last_segment();
        if (!inner.currentjp.empty())
          inner.currentjp.remove_scope();
        // при "переезде" в другой файл name теряет значение
        refvtor{inner}(lastseg, resolved);
        return;
      } else {
        // вызывается чтобы обойти всё и собрать упоминающиеся ссылки
        ctx->resolve_schema_ref(*r, obj_file, currentjp / name);
      }
    } else {
      recursion(name, *std::get_if<T>(&x));
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
        visitor(name, cb, std::move(file), std::move(jp) / methodstr / "callbacks" / name);
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
      visitor(name, cb, file, "/components/callbacks"_jp);
  }

  // components/pathItems
  if (api.components->pathItems) {
    for (auto& [name, item] : *api.components->pathItems) {
      auto [ritem, reffile, refjp] = resolve_ref_and_ret(item, file, "/components/pathItems"_jp, ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjp));
    }
  }

  // paths -> operations -> callbacks
  if (api.paths) {
    for (auto& [path, item] : *api.paths) {
      auto [ritem, reffile, refjp] = resolve_ref_and_ret(item, file, "/paths"_jp / path, ctx);
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
                                                      json_pointer jp, std::string_view content_type) {
  ir::body result;
  // возможно media type range
  result.contentType = content_type;
  if (obj.schema) {
    auto [ref, rschema] = resolve_schema(*obj.schema, file, jp / "schema", *this);
    result.schema = ref;
    if (obj.encoding) {
      auto _ = guard_array(result.membersEncoding);
      // есть только если есть schema (валидируется до этого)
      // наличие property в схеме не проверяется, так как там очень сложно - может быть в anyOf и тд
      // где-то
      for (auto& [property, enc] : *obj.encoding) {
        if (!enc.contentType)
          continue;  // остальное пока игнорируется (всякие style и тд)
        result.membersEncoding->try_emplace(property, *enc.contentType);
      }
    }
  }
  // result.streaming пока игнорируется
  return result;
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
    for (auto& [name, hdr] : *rsp.headers) {
      ir::header result_hdr;
      result_hdr.name = name;
      auto [hdrobj, reffile, refjp] = resolve_ref_and_ret(hdr, file, jp / "headers" / name, *this);
      if (hdrobj.required)
        result_hdr.required = *hdrobj.required;
      if (hdrobj.schema)
        result_hdr.schema = resolve_schema(*hdrobj.schema, reffile, refjp / "schema", *this).ir_schema;
      result.headers->insert(std::move(result_hdr));
    }
  }
  if (rsp.content) {
    auto _ = guard_array(result.bodies);
    for (auto& [contenttype, bodydesc] : *rsp.content)
      result.bodies->insert(
          media_type_object_lowering(bodydesc, file, jp / "content" / contenttype, contenttype));
  }

  return result;
}

// TODO сделать required по дефолту если присутствует в path и валидировать что оно есть в строке path
ir::operation_parameter lowering_context::parameter_lowering(const parameter_object& param, fs::path file,
                                                             json_pointer jp, std::string_view path) {
  ir::operation_parameter result;
  // TODO здесь где то со скоупом накосячено
  // а может и не здесь... А там где первый раз резолвятся эти штуки
  result.location = param.in;
  result.name = param.name;
  if (param.schema)
    result.schema = resolve_schema(*param.schema, file, jp / "schema", *this).ir_schema;
  else {
    // валидация была
    assert(param.content && param.content->size() == 1);
    // content type
    auto& [content_type, mediaobj] = *param.content->begin();
    result.encoding = content_type;
    if (!mediaobj.schema)
      throw validate_error("parameter schema not setted. Both `schema` and `content/schema` not setted");
    result.schema =
        resolve_schema(*mediaobj.schema, file, jp / "content" / content_type / "schema", *this).ir_schema;
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
  // TODO result.meta
  return result;
}
std::unordered_set<ir::body> lowering_context::request_body_lowering(request_body_object obj, fs::path file,
                                                                     json_pointer jp) {
  std::unordered_set<ir::body> result;
  for (auto& [content_type, mediaobj] : obj.content)
    result.insert(media_type_object_lowering(mediaobj, file, jp / "content" / content_type, content_type));
  return result;
}

string_map<ir::operation> lowering_context::path_item_object_lowering(std::string_view path, fs::path spec,
                                                                      fs::path resolve_ref_file,
                                                                      json_pointer jp, std::string_view name,
                                                                      path_item_object item) {
  string_map<ir::operation> result;

  for (auto [methodstr, o] : item.all_operations_with_method()) {
    ir::operation op;
    op.spec = spec.generic_string();
    if (o.operationId)
      op.id = *o.operationId;
    else {
      op.id = std::string(methodstr);
      *op.id += name;
    }
    op.method = methodstr;
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

    // не зависит от порядка если чё
    result.try_emplace(op.method, std::move(op));
  }
  // TODO op.meta

  return result;
}

ir::ir_object lowering_context::visit_and_generate_ir(const openapi_object& api, const fs::path& obj_file) {
  ir::ir_object result;

  // собираем все схемы, в том числе из калбеков, вебхуков и paths
  schemas_collector collector(obj_file, result.schemas, *this);
  // TODO postparse надо на чето заменить видимо, чтобы парс + сериализация не меняли
  // TODO в моём языке разрешить ссылки на фундаментальные типы: например #string, #number и тд
  // чтобы например легко писать oneOf: - #string -#number
  for_each_field_with_name(api, refvtor(collector));

  visit_all_callbacks(
      api, obj_file, *this,
      [&](std::string_view name, const reference_or<callback_object>& cb, fs::path cbpath_, json_pointer jp) {
        auto [rcb, cbpath, cbjp] = resolve_ref_and_ret(cb, cbpath_, jp, *this);
        // TODO not ignore `expr` (складывать в список для дальнейшей генерации)
        for (auto&& [expr, pathItem] : rcb) {
          // используем cbpath, т.к. ссылки могут указывать туда если он загружен оттуда
          auto [rpi, rpipath, rpijp] = resolve_ref_and_ret(pathItem, cbpath, cbjp / expr, *this);
          // используется путь откуда операция взялась, но она всё ещё декларирована в
          // obj_file и является операцией этого апи
          auto ops = path_item_object_lowering(
              /*path=*/"", obj_file, rpipath, std::move(rpijp), name, std::move(rpi));
          for (auto& [method, op] : ops)
            result.webhooks.insert(std::move(op));
        }
      });

  if (api.paths) {
    // если были pathItems и на них ссылались и в paths и в webhooks, то они продублируются на уровне IR
    // но это нормально
    for (auto& [path, item] : *api.paths) {
      auto [ritem, file, item_jp] = resolve_ref_and_ret(item, obj_file, "/paths"_jp / path, *this);
      string_map<std::string> name2path;
      string_map<std::string> path2name;
      std::string generatedname = path_to_name(path, name2path, path2name);
      auto ops = path_item_object_lowering(path, obj_file, file, std::move(item_jp), generatedname,
                                           std::move(ritem));
      for (auto& [method, op] : ops)
        result.operations.insert(op);
    }
  }
  return result;
}

}  // namespace svarg

// TODO перенести файлы в openapi_frontend
