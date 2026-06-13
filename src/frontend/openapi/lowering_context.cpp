#include "svarg/frontend/openapi/lowering_context.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/merge_with_override.hpp"
#include "svarg/utils/scopes.hpp"

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
static void remap_references(string_map<Any>& fields, const fs::path& curfile, const lowering_context& ctx) {
  for (auto& [key, a] : fields) {
    auto remap_ref = [&](boost::json::value& ref) {
      if (!ref.is_string())
        throw parse_error("invalid reference, expected string, but got {}", e2str(ref.kind()));
      parsed_ref r = parse_ref(ref.as_string());
      fs::path p = ctx.find_file_from_ref(r.file, curfile);
      ref.as_string() = std::format("{}#{}", p.generic_string(), r.json_ptr);
    };
    visit_refs(a.value, remap_ref);
  }
}

// TODO тест на ремап ссылок в resolve_ref_to учитывая инклуд директории и так далее

// рекурсивно резолвит ссылку и в конце передаёт в invocable объект T, путь в котором он лежал (относительно
// specs_dir) и json pointer на него в этом файле
// возвращает то что вернёт вызов invocable(tvalue, file, jsonptr)
// в поданном в конце объекте все ссылки относительно его файла ЛИБО абсолютные
// (те которые прицепились к нему по дороге)
template <typename T>
auto resolve_ref_to(reference_object ref, fs::path file, auto&& invocable, lowering_context& ctx) {
  parsed_ref r;
  // здесь нельзя кешировать, так как с разных мест разные ссылки дают разный результат из-за override fields

  // поля со всех ссылок складываются сюда. $ref + override FIELD1
  // ссылается на $ref + override FIELD2 и так далее
  string_map<Any> override_fields = std::move(ref._keyfield_override_fields);
  // override fields написаны в одном файле, а резолвится будут возможно в контексте
  // другого поэтому их нужно перемапить на "универсальную" основу не зависящую от конкретного файла
  remap_references(override_fields, file, ctx);
  for (size_t recursion_depth = 0; recursion_depth <= ctx.opts.ref_max_depth; ++recursion_depth) {
    if (!ref.ref_.contains("#/components/")) [[unlikely]] {
      ctx.emit_warn(file, r.json_ptr.empty() ? parse_ref(ref.ref_).json_ptr : r.json_ptr,
                    "$ref should be into /components/, but $ref = {}", ref.ref_);
    }
    r = parse_ref(ref.ref_);
    fs::path refered_file = ctx.find_file_from_ref(r.file, file);
    const boost::json::value& v = ctx.load_as_json(refered_file);
    boost::json::value jref = v.at_pointer(r.json_ptr);
    if (!jref.is_object())
      throw semantic_error("refered by $ref must be json/yaml object, but got {}", e2str(jref.kind()));
    for (auto& [k, j] : override_fields)
      jref.as_object()[k] = j.value;
    auto ref_or_t = parse_from_json<reference_or<T>>(jref);
    if (auto* val = std::get_if<T>(&ref_or_t))
      return invocable(std::move(*val), std::move(refered_file), r.json_ptr);
    ref = std::get<reference_object>(std::move(ref_or_t));
    file = std::move(refered_file);
    remap_references(ref._keyfield_override_fields, file, ctx);
    override_fields =
        merge_with_override(std::move(override_fields), std::move(ref._keyfield_override_fields));
  }
  throw semantic_error("recursion depth max reached");
}

template <typename T>
object_and_path<T> resolve_ref_and_ret(reference_or<T> x, fs::path file, scopes_t jsonptr,
                                       lowering_context& ctx) {
  if (auto* r = std::get_if<reference_object>(&x)) {
    auto getcb = [](T obj, fs::path file, std::string_view rjsonptr) {
      return object_and_path(std::move(obj), std::move(file), rjsonptr);
    };
    return resolve_ref_to<T>(std::move(*r), std::move(file), getcb, ctx);
  } else {
    return object_and_path(std::move(std::get<T>(x)), std::move(file), std::move(jsonptr));
  }
}

// jsonptr используется только если это инлайн схема для создания уникального scope
// например jsonptr = /components/schemas и fieldname = "additionalProperties"
[[nodiscard]] static resolved_schema resolve_schema(const reference_or<schema_object>& sor,
                                                    const fs::path& file, scopes_t jsonptr,
                                                    lowering_context& ctx) {
  if (auto* ref = std::get_if<reference_object>(&sor)) {
    return ctx.resolve_schema_ref(*ref, file);
  } else {
    auto s = ctx.schema_lowering(std::get<schema_object>(sor), file, std::move(jsonptr), std::nullopt);
    return {std::move(s).ref(), std::get<schema_object>(sor)};
  }
}

ir::object_schema lowering_context::make_object_type(schema_object const& schema, const fs::path& file,
                                                     scopes_t jsonptr) {
  ir::object_schema ot;
  if (schema.properties) {
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      ir::object_field_info fi;
      fi.name = prop_name;
      auto [schemaref, field_schema] =
          resolve_schema(prop_sor, file, jsonptr / "properties" / prop_name, *this);
      fi.type = schemaref;
      if (field_schema.nullable || field_schema.default_)
        fi.required = false;
      else if (schema.required) {
        if (field_schema.nullable || field_schema.default_) {
          emit_warn(file, jsonptr.view(), "property {}` marked as required, but {}. `required` ignored",
                    prop_name, field_schema.nullable ? "has nullable type" : "has default");
        } else if (field_schema.is_write_only() || field_schema.is_read_only()) {
          // readOnly/writeOnly означает, что по сути есть два типа - для запроса и ответа и required зависит
          // от этого здесь генерируется только один тип, поэтому оба readOnly/writeOnly означают по факту
          // опциональность поля
          emit_warn(file, jsonptr.view(), "property {} marked as {}, `required` ignored", prop_name,
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
        resolve_schema(*schema.additionalProperties, file, jsonptr / "additionalProperties", *this).ir_schema;
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
[[nodiscard]] static size_t count_true(optional<Args> const&... args) noexcept {
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
                                                scopes_t jsonptr, std::optional<std::string_view> name) {
  ir::schema res;
  res.scope = std::format("{}#{}", file.generic_string(), jsonptr.view());
  auto ret_res = [&] {
    ir.schemas.insert(res);
    return schema_or_ref(std::move(res));
  };
  if (name)
    res.name = *name;
  else
    res.name = std::nullopt;
  if (auto it = ir.schemas.find(res); it != ir.schemas.end())
    return schema_or_ref{*it};
  const strpair cache_key{res.scope, name ? *name : ""};
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
      throw semantic_error("enum at `{}` must have at least one value", jsonptr.view());
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
        res.data = make_object_type(schema, file, jsonptr);
      } else if (t == "array") {
        ir::array_schema at;
        if (!schema.items)
          throw parse_error("schema with type 'array' must contain 'items'");
        at.element = resolve_schema(*schema.items, file, jsonptr / "items", *this).ir_schema;
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

  auto collect_result = [&](std::string_view jsonptr, auto schema) {
    if (!as_requirements) {
      res.data = std::move(schema);
    } else {
      ir::schema s;
      // создаём что-то типа file#abc/anyOf
      if (!s.scope.ends_with('/'))
        s.scope = res.scope + "/" + std::string(jsonptr);
      else
        s.scope = res.scope + std::string(jsonptr);
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
      resolved_schema s = resolve_schema(from[i], file, jsonptr / ptr / i, *this);
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
    t.type = resolve_schema(*schema.not_, file, jsonptr / "not", *this).ir_schema;
    collect_result("not", std::move(t));
  }
  return ret_res();
}

// ожидает что file относительно spec dir
resolved_schema lowering_context::resolve_schema_ref(reference_object ref, fs::path file) {
  auto doschema = [&](schema_object schema, fs::path refered_file, std::string_view jsonptr) {
    // это не инлайн схема, так как мы смогли на неё сослаться, т.е. у неё было имя (хотя по коду подойдёт
    // любой json ptr, но это... такое). Если ссылались не на components/schemas, то варнинг уже был
    std::string_view name = last_segment(jsonptr);
    if (name.empty())
      throw semantic_error("name of schema cannot be empty. json pointer: {}", jsonptr);
    resolved_schema res;
    res.ir_schema = schema_lowering(schema, refered_file, jsonptr, name).ref();
    res.front_schema = std::move(schema);
    return res;
  };
  return resolve_ref_to<schema_object>(std::move(ref), std::move(file), doschema, *this);
}

// оборачивает визитор в ссылке с тем же интерфейсом,
// чтобы копирование результирующей структуры не ломало внутренние данные vtor (н-р если он не умеет
// копироваться) нужно из-за boost::pfr::for_each<***> которые копируют визитор
constexpr auto refvtor(auto& vtor) {
  return [&](std::string_view name, auto&& arg) -> decltype(auto) {
    return vtor(name, std::forward<decltype(arg)>(arg));
  };
}

// пылесосит объект в поисках всех схем, в том числе в других файлах на которые ссылаются $ref
struct schemas_collector {
 private:
  const fs::path& obj_file;
  lowering_context* ctx;
  std::unordered_set<ir::schema>& schemas;
  scopes_t scopes;

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
    // for (std::string_view s : scopes.segments())
    //   ss << s << '.';
    // ss << name << '\n';
  }
  template <typename T>
  void operator()(std::string_view name, const array<T>& f) try {
    dbgprint(name);
    scopes.add_scope(name);
    on_scope_exit {
      scopes.remove_scope();
    };
    for (size_t i = 0; i < f.size(); ++i)
      // не смотря на то что скоп может быть захвачен по string_view,
      // время жизни строки из format здесь должно быть валидно везде
      (*this)(std::format("{}", i), f[i]);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name)

  template <typename T>
  void operator()(std::string_view name, const string_map<T>& f) try {
    dbgprint(name);
    scopes.add_scope(name);
    on_scope_exit {
      scopes.remove_scope();
    };
    for (auto& [key, v] : f)
      (*this)(std::string_view(key), v);
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
  void operator()(std::string_view name, const aggregate auto& x) try {
    scopes.add_scope(name);
    dbgprint(name);
    on_scope_exit {
      scopes.remove_scope();
    };
    boost::pfr::for_each_field_with_name(x, refvtor(*this));
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  void operator()(std::string_view name, const schema_object& x) try {
    dbgprint(name);
    // схемы могут встретиться в:
    // * parameter_object/header_object/media_type_object - там они инлайн
    // * components_object (components/schemas) - там они не инлайн
    // * в самих schema_object - их вообще этот визитор не посещает, это обрабатывается в schema_lowering
    std::optional<std::string_view> schema_name;
    if (scopes.view().ends_with("/components/schemas"))
      schema_name = name;
    ctx->schema_lowering(x, obj_file, std::format("{}/{}", scopes.view(), name), schema_name);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  template <typename T>
  void operator()(std::string_view name, const reference_or<T>& x) {
    dbgprint(name);

    if (auto* r = std::get_if<reference_object>(&x)) {
      if constexpr (!std::is_same_v<T, schema_object>) {
        // проходимся на зарезолвленном объекте, чтобы собрать всё что упомянуто там
        auto [resolved, path, jsonptr] = resolve_ref_and_ret(x, obj_file, scopes, *ctx);
        schemas_collector inner(path, schemas, *ctx);
        inner.scopes = std::move(jsonptr);
        inner(name, resolved);
        return;
      } else {
        // вызывается чтобы обойти всё и собрать упоминающиеся ссылки
        ctx->resolve_schema_ref(*r, obj_file);
      }
    } else {
      (*this)(name, *std::get_if<T>(&x));
    }
  }

  template <typename T>
  void operator()(std::string_view name, const optional<T>& x) {
    if (x)
      (*this)(name, *x);
  }
};

static void visit_path_item_callbacks(const path_item_object& item, auto&& visitor, fs::path file,
                                      scopes_t jsonptr) {
  for (auto& op : item.all_operations()) {
    if (op && op->callbacks) {
      for (auto& [name, cb] : *op->callbacks)
        visitor(name, cb, std::move(file), std::move(jsonptr) / "callbacks" / name);
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
      visitor(name, cb, file, "/components/callbacks");
  }

  // components/pathItems
  if (api.components->pathItems) {
    for (auto& [name, item] : *api.components->pathItems) {
      auto [ritem, reffile, refjsonptr] = resolve_ref_and_ret(item, file, "/components/pathItems", ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjsonptr));
    }
  }

  // paths -> operations -> callbacks
  if (api.paths) {
    for (auto& [path, item] : *api.paths) {
      auto [ritem, reffile, refjsonptr] = resolve_ref_and_ret(item, file, scopes_t("/paths") / path, ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjsonptr));
    }
  }

  // webhooks -> operations -> callbacks
  if (api.webhooks) {
    for (auto& [name, item] : *api.webhooks) {
      auto [ritem, reffile, refjsonptr] = resolve_ref_and_ret(item, file, scopes_t("/webhooks") / name, ctx);
      visit_path_item_callbacks(ritem, visitor, std::move(reffile), std::move(refjsonptr));
    }
  }
}
// TODO?  если вообще все ссылки ремапить сразу, то как будто легче код становится сразу
// TODO в интерфейс принимать сразу все файлы которые нужно сгенерировать и объединить процесс
// загрузки всех файлов, предобработки (и там ремап ссылок абсолютно всех даже в файлах НА которые ссылки)
// и затем generate, в итоге один метод есть просто

// statusstr пустой для дефолтного респонса
ir::response_desc lowering_context::response_lowering(std::optional<std::string_view> statusstr,
                                                      const response_object& rsp, fs::path file,
                                                      scopes_t jsonptr) {
  ir::response_desc result;
  if (statusstr)
    result.set_status(*statusstr);
  else
    result.make_default();
  result.bodies;   // rsp.content
  result.headers;  // rsp.headers

  if (rsp.headers) {
    // TODO in header
    for (auto& [name, hdr] : *rsp.headers) {
      ir::header result_hdr;
      result_hdr.name = name;
      auto [hdrobj, reffile, refjsonptr] = resolve_ref_and_ret(hdr, file, jsonptr / "headers" / name, *this);
      if (hdrobj.schema) {
        resolved_schema rs = resolve_schema(*hdrobj.schema, reffile, refjsonptr, *this);
      }
    }
  }

  return result;  // TODO
}

// TODO сделать required по дефолту если присутствует в path и валидировать что оно есть в строке path
ir::operation_parameter lowering_context::parameter_lowering(const parameter_object& param,
                                                             std::string_view path) {
  return {};  // TODO
}
std::unordered_set<ir::body> lowering_context::body_lowering(request_body_object) {
  return {};  // TODO
}

string_map<ir::operation> lowering_context::path_item_object_lowering(std::string_view path, fs::path spec,
                                                                      fs::path resolve_ref_file,
                                                                      scopes_t jsonptr, std::string_view name,
                                                                      path_item_object item) {
  string_map<ir::operation> result;

  for (auto [o, methodstr] : item.all_operations_with_method()) {
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
        auto [obj, reffile, refjsonptr] = resolve_ref_and_ret(*o.responses->default_, resolve_ref_file,
                                                              jsonptr / "responses" / "default", *this);
        op.defaultResponse = response_lowering(std::nullopt, obj, std::move(reffile), std::move(refjsonptr));
      }
      op.responses.emplace();
      for (auto&& [status, r] : o.responses->_responses) {
        auto [obj, reffile, refjsonptr] =
            resolve_ref_and_ret(r, resolve_ref_file, jsonptr / "responses" / status, *this);
        ir::response_desc rsp = response_lowering(status, obj, std::move(reffile), std::move(refjsonptr));
        op.responses->insert(std::move(rsp));
      }
      if (op.responses->empty())
        op.responses.reset();
    }
    if (o.parameters) {
      op.parameters.emplace();
      size_t i = 0;
      for (auto& param : *o.parameters) {
        ir::operation_parameter p = parameter_lowering(
            resolve_ref_and_ret(param, resolve_ref_file, jsonptr / "parameters" / i, *this).object, path);
        op.parameters->push_back(std::move(p));
        ++i;
      }
      if (op.parameters->empty())
        op.parameters.reset();
    }
    if (o.requestBody) {
      op.bodies = body_lowering(
          resolve_ref_and_ret(*o.requestBody, resolve_ref_file, jsonptr / "requestBody", *this).object);
      if (op.bodies->empty())
        op.bodies.reset();
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
  boost::pfr::for_each_field_with_name(api, refvtor(collector));

  visit_all_callbacks(api, obj_file, *this,
                      [&](std::string_view name, const reference_or<callback_object>& cb, fs::path cbpath_,
                          scopes_t jsonptr) {
                        auto [rcb, cbpath, cbjsonptr] = resolve_ref_and_ret(cb, cbpath_, jsonptr, *this);
                        // TODO not ignore `expr` (складывать в список для дальнейшей генерации)
                        for (auto&& [expr, pathItem] : rcb) {
                          // используем cbpath, т.к. ссылки могут указывать туда если он загружен оттуда
                          auto [rpi, rpipath, rpijsonptr] =
                              resolve_ref_and_ret(pathItem, cbpath, cbjsonptr, *this);
                          // используется путь откуда операция взялась, но она всё ещё декларирована в
                          // obj_file и является операцией этого апи
                          auto ops = path_item_object_lowering(
                              /*path=*/"", obj_file, rpipath, std::move(rpijsonptr), name, std::move(rpi));
                          for (auto& [method, op] : ops)
                            result.webhooks.insert(std::move(op));
                        }
                      });

  if (api.paths) {
    // если были pathItems и на них ссылались и в paths и в webhooks, то они продублируются на уровне IR
    // но это нормально
    for (auto& [path, item] : *api.paths) {
      auto [ritem, file, itemjsonptr] = resolve_ref_and_ret(item, obj_file, scopes_t("/paths") / path, *this);
      string_map<std::string> name2path;
      string_map<std::string> path2name;
      std::string generatedname = path_to_name(path, name2path, path2name);
      auto ops = path_item_object_lowering(path, obj_file, file, std::move(itemjsonptr), generatedname,
                                           std::move(ritem));
      for (auto& [method, op] : ops)
        result.operations.insert(op);
    }
  }
  return result;
}

}  // namespace svarg

// TODO перенести файлы в openapi_frontend
