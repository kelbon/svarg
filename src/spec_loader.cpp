#include "svarg/spec_loader.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/merge_with_override.hpp"
#include "svarg/utils/scopes.hpp"
#include "svarg/utils/all.hpp"

#include <boost/pfr.hpp>

namespace svarg {

// рекурсивно резолвит ссылку и в конце передаёт в invocable объект T, путь в котором он лежал (относительно
// specs_dir) и json pointer на него в этом файле
// возвращает то что вернёт вызов invocable(tvalue, file, jsonptr)
template <typename T>
static auto resolve_ref_to(reference_object ref, std::filesystem::path file, auto&& invocable,
                           spec_loader& loader) {
  parsed_ref r;
  // здесь нельзя кешировать, так как с разных мест разные ссылки дают разный результат из-за override fields

  // поля со всех ссылок складываются сюда. $ref + override FIELD1
  // ссылается на $ref + override FIELD2 и так далее
  string_map<Any> override_fields = std::move(ref._keyfield_override_fields);
  for (size_t recursion_depth = 0; recursion_depth <= loader.get_options().ref_max_depth; ++recursion_depth) {
    if (!ref.ref_.contains("#/components/")) [[unlikely]] {
      loader.emit_warn(file, r.json_ptr.empty() ? parse_ref(ref.ref_).json_ptr : r.json_ptr,
                       "$ref should be into /components/, but $ref = {}", ref.ref_);
    }
    r = parse_ref(ref.ref_);
    std::filesystem::path refered_file =
        std::filesystem::relative(!r.file.empty() ? file.parent_path() / r.file : file);
    refered_file = loader.find_existing_file(refered_file);
    const boost::json::value& v = loader.load_as_json(refered_file);
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
    override_fields =
        merge_with_override(std::move(override_fields), std::move(ref._keyfield_override_fields));
  }
  throw semantic_error("recursion depth max reached");
}

// jsonptr и fieldname используются только если это инлайн схема для создания уникального scope
// например jsonptr = /components/schemas и fieldname = "additionalProperties"
[[nodiscard]] static resolved_schema resolve_schema(const reference_or<schema_object>& sor,
                                                    const std::filesystem::path& file,
                                                    std::string_view jsonptr, std::string_view fieldname,
                                                    std::unordered_set<ir::schema>& schemas,
                                                    spec_loader& loader) {
  if (auto* ref = std::get_if<reference_object>(&sor)) {
    return loader.resolve_schema_ref(*ref, file, schemas);
  } else {
    if (jsonptr.ends_with('/'))
      jsonptr.remove_suffix(1);
    std::string cur_jsonptr = std::format("{}/{}", jsonptr, fieldname);
    auto s = loader.schema_lowering(std::get<schema_object>(sor), file, cur_jsonptr, std::nullopt, schemas);
    return {std::move(s).ref(), std::get<schema_object>(sor)};
  }
}

ir::object_schema spec_loader::make_object_type(schema_object const& schema,
                                                const std::filesystem::path& file, std::string_view jsonptr,
                                                std::unordered_set<ir::schema>& schemas) {
  if (jsonptr.ends_with('/'))
    jsonptr.remove_suffix(1);
  ir::object_schema ot;
  if (schema.properties) {
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      // synthetic jsonptr для свойства: jsonptr + "/properties/" + prop_name
      std::string prop_ptr = std::format("properties/{}", prop_name);
      ir::object_field_info fi;
      fi.name = prop_name;
      auto [schemaref, field_schema] = resolve_schema(prop_sor, file, jsonptr, prop_ptr, schemas, *this);
      fi.type = schemaref;
      if (field_schema.nullable || field_schema.default_)
        fi.required = false;
      else if (schema.required) {
        if (field_schema.nullable || field_schema.default_) {
          emit_warn(file, jsonptr, "property {}` marked as required, but {}. `required` ignored", prop_name,
                    field_schema.nullable ? "has nullable type" : "has default");
        } else if (field_schema.is_write_only() || field_schema.is_read_only()) {
          // readOnly/writeOnly означает, что по сути есть два типа - для запроса и ответа и required зависит
          // от этого здесь генерируется только один тип, поэтому оба readOnly/writeOnly означают по факту
          // опциональность поля
          emit_warn(file, jsonptr, "property {} marked as {}, `required` ignored", prop_name,
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
        resolve_schema(*schema.additionalProperties, file, jsonptr, "additionalProperties", schemas, *this)
            .ir_schema;
  }
  return ot;
}

static ir::discriminator_desc discriminator_lowering(const discriminator_object& d,
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
schema_or_ref spec_loader::schema_lowering(const schema_object& schema, const std::filesystem::path& file,
                                           std::string_view jsonptr, std::optional<std::string_view> name,
                                           std::unordered_set<ir::schema>& schemas) {
  if (!jsonptr.starts_with('/'))
    throw semantic_error("invalid part of $ref after # (`{}`), must start with /", jsonptr);
  ir::schema res;
  res.scope = std::format("{}#{}", file.generic_string(), jsonptr);
  auto ret_res = [&] {
    schemas.insert(res);
    return schema_or_ref(std::move(res));
  };
  if (name)
    res.name = *name;
  else
    res.name = std::nullopt;
  if (auto it = schemas.find(res); it != schemas.end())
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
      throw semantic_error("enum at `{}` must have at least one value", jsonptr);
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
        res.data = make_object_type(schema, file, jsonptr, schemas);
      } else if (t == "array") {
        ir::array_schema at;
        if (!schema.items)
          throw parse_error("schema with type 'array' must contain 'items'");
        at.element = resolve_schema(*schema.items, file, jsonptr, "items", schemas, *this).ir_schema;
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
      schemas.insert(std::move(s));
      res.add_requirement(std::move(r));
    }
  };

  // ── логические комбинаторы ──────────────────────────────────────────────────

  auto resolve_array = [&](const array<reference_or<schema_object>>& from, array<ir::schema_ref>& to) {
    for (size_t i = 0; i < from.size(); ++i) {
      // _{index} чтобы имя было валидно в языках программирования
      std::string inline_name = std::format("_{}", i);
      resolved_schema s = resolve_schema(from[i], file, jsonptr, inline_name, schemas, *this);
      to.push_back(s.ir_schema);
    }
  };

  if (schema.allOf) {
    ir::allof_schema t;
    resolve_array(*schema.allOf, t.schemas);
    collect_result("allOf", std::move(t));
  }
  if (schema.anyOf) {
    ir::anyof_schema t;
    resolve_array(*schema.anyOf, t.schemas);
    if (schema.discriminator)
      t.discriminator = discriminator_lowering(*schema.discriminator, t.schemas);
    collect_result("anyOf", std::move(t));
  }
  if (schema.oneOf) {
    ir::oneof_schema t;
    resolve_array(*schema.oneOf, t.schemas);
    if (schema.discriminator)
      t.discriminator = discriminator_lowering(*schema.discriminator, t.schemas);
    collect_result("oneOf", std::move(t));
  }
  if (schema.not_) {
    ir::not_schema t;
    t.type = resolve_schema(*schema.not_, file, jsonptr, "not", schemas, *this).ir_schema;
    collect_result("not", std::move(t));
  }
  return ret_res();
}

// ожидает что file относительно spec dir
resolved_schema spec_loader::resolve_schema_ref(reference_object ref, std::filesystem::path file,
                                                std::unordered_set<ir::schema>& schemas) {
  auto doschema = [&](schema_object schema, std::filesystem::path refered_file, std::string_view jsonptr) {
    // это не инлайн схема, так как мы смогли на неё сослаться, т.е. у неё было имя (хотя по коду подойдёт
    // любой json ptr, но это... такое). Если ссылались не на components/schemas, то варнинг уже был
    std::string_view name = last_segment(jsonptr);
    if (name.empty())
      throw semantic_error("name of schema cannot be empty. json pointer: {}", jsonptr);
    resolved_schema res;
    res.ir_schema = schema_lowering(schema, refered_file, jsonptr, name, schemas).ref();
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

// TODO? RM What?
// посещает все ссылки на `What` и передаёт калбеку json-pointer до элемента и сам элемент
template <typename What>
struct vtor {
  std::filesystem::path obj_file;
  spec_loader* loader;
  std::unordered_set<ir::schema> schemas;
  scopes_t scopes;

  explicit vtor(std::filesystem::path obj_file, spec_loader& l) : obj_file(std::move(obj_file)), loader(&l) {
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
    loader->schema_lowering(x, obj_file, std::format("{}/{}", scopes.view(), name), schema_name, schemas);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  template <typename T>
  void operator()(std::string_view name, const reference_or<T>& x) {
    dbgprint(name);

    if (auto* r = std::get_if<reference_object>(&x)) {
      // TODO тут явно надо подумать что с другими делать (ссылки на параметры и тд)
      // TODO как минимум нужно заходить в ссылки на параметры и хедеры, чтобы там инлайн схемы искать или чёт
      // такое
      if constexpr (!std::is_same_v<T, What>)
        return;
      // вызывается чтобы обойти всё и собрать упоминающиеся ссылки
      loader->resolve_schema_ref(*r, obj_file, schemas);
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

static void visit_path_item_callbacks(const path_item_object& item, auto&& visitor) {
  for (auto& op :
       all(item.get, item.put, item.post, item.delete_, item.options, item.head, item.patch, item.trace)) {
    if (op && op->callbacks) {
      for (auto& [name, cb] : *op->callbacks)
        visitor(name, cb);
    }
  }
}

// посещает все калбеки и передаёт визитору имя + reference_or<callback>
// Note: может быть много калбеков с одинаковым именем. Они отличаются по методу и тд
static void visit_all_callbacks(const openapi_object& api, auto&& visitor) {
  // components/callbacks
  if (api.components && api.components->callbacks) {
    for (auto& [name, cb] : *api.components->callbacks)
      visitor(name, cb);
  }

  // components/pathItems
  if (api.components->pathItems) {
    for (auto& [name, item] : *api.components->pathItems)
      visit_path_item_callbacks(item, visitor);
  }

  // paths -> operations -> callbacks
  if (api.paths) {
    for (auto& [path, item] : *api.paths)
      visit_path_item_callbacks(item, visitor);
  }

  // webhooks -> operations -> callbacks
  if (api.webhooks) {
    for (auto& [name, item] : *api.webhooks)
      visit_path_item_callbacks(item, visitor);
  }
}

// TODO: реюз resolve_ref
// TODO: path item ставим где надо | reference и меняем его парсинг в обычные правила

// возвращает path_item_object где точно нет $ref
path_item_object resolve_path_item_object(const path_item_object& item, const std::filesystem::path& file) {
  // TODO
  return {};
}

// name - operationId для операций, имя калбека/вебхука для калбека или вебхука
// path - путь относительно сервера для операции, runtime expr для калбека
ir::operation path_item_object_lowering(std::string_view path, std::optional<std::string_view> name,
                                        path_item_object) {
  // TODO
  return {};
}

callback_object spec_loader::resolve_callback(reference_or<callback_object> cb, std::filesystem::path file) {
  if (auto* r = std::get_if<reference_object>(&cb)) {
    auto getcb = [](callback_object obj, std::filesystem::path file, std::string_view jsonptr) {
      return obj;
    };
    return resolve_ref_to<callback_object>(std::move(*r), std::move(file), getcb, *this);
  } else {
    return std::move(std::get<callback_object>(cb));
  }
}

ir::ir_object spec_loader::visit_and_generate_ir(const openapi_object& obj,
                                                 const std::filesystem::path& obj_file) {
  ir::ir_object result;

  // собираем все схемы, в том числе из калбеков, вебхуков и paths
  vtor<schema_object> v(obj_file, *this);
  // TODO postparse надо на чето заменить видимо, чтобы парс + сериализация не меняли
  // TODO в моём языке разрешить ссылки на фундаментальные типы: например #string, #number и тд
  // чтобы например легко писать oneOf: - #string -#number
  boost::pfr::for_each_field_with_name(obj, refvtor(v));
  result.schemas = std::move(v.schemas);

  visit_all_callbacks(obj, [&](std::string_view name, const reference_or<callback_object>& cb) {
    auto getcb = [](callback_object obj, std::filesystem::path file, std::string_view jsonptr) {
      return obj;
    };
    callback_object obj = resolve_callback(cb, obj_file);
    // TODO not ignore `expr` (складывать в список для дальнейшей генерации)
    for (auto&& [expr, pathItem] : obj) {
      path_item_object rpi = resolve_path_item_object(pathItem, obj_file);
      ir::operation op = path_item_object_lowering(expr, name, std::move(rpi));
      result.webhooks.insert(std::move(op));
    }
  });

  // ! либо не перекладывать никуда калбеки, просто их обойти. И складывать в webhooks
  // TODO надо операции сравнение и хеш сделать
  // TODO на первом этапе как будто надо переложить калбеки в вебхуки
  //  калбеки есть только в операциях (которые в paths & components/pathItems и в components/callbacks) и в
  //  webhooks (тоже в операциях)
  //  визитим все ref_or_калбеки, если они значения - собираем в мапу (так чтобы одинаковые не попадали
  //  дважды) если видим ссылку, то если локальная - ничего не делаем, этот калбек соберут итак. Если в другой
  //  файл и ещё не собрали этот калбек - идём резолвить ссылку и складываем его значение в вебхуки
  //  исправляя локальные ссылки в нём TODO стоп, не только локальные ссылки, они же все относительные,
  //  сложнее немного.. Надо для каждой ссылки считать куда указывает file_with_callback/$reffile и если его
  //  нет искать существующий $reffile и ставить относительно specs_dir

  // второй этап - резолв ссылок в webhooks/paths на pathItems с переносом (дублированием) просто напросто.
  // третий этап - последовательно преобразовать все path item в webhooks/path (lowering path item)
  // это включает в себя резолв $ref, резолв параметров и тд. Там вроде рекурсии быть не может... Хотя хз
  // в общем надо получить просто конечный json распаршенный для параметров, хедеров и тд

  // если с другого файла перенос, то локальные ссылки не меняются, если с другого - меняются
  // второй этап (хотя желательно ДО) - все pathItems типа переиспользуемые... продублировать в местах где на
  // них ссылаются (paths/webhooks) чтобы не городить ссылки (хотя мб сделать ссылку на операцию?..). А если
  // ссылка в другом файле на это... То плевать, надо просто зарезолвить и сделать так чтобы не было ссылок
  // никаких. НО если втупую продублировать, то сломаются ссылки внутри объекта... Значит именно их надо
  // поменять (fix local references по сути пройти по всем и поменять ссылки)
  // TODO: сделать тест на рекурсивные типы (напрямую, косвенно, между файлами)
  // TODO: сделать тест на парсинг калбеков, вебхуков, pathItems и ссылки и так далее
  // TODO: записывать тест кейсы везде, чтобы позже реализовать
  // TODO надо пройти по операциями, калбекам и вебхукам
  // и конвертировать их в IR. Операции - в operation, калбеки и вебхуки в webhooks
  // т.е. по сути нужно посещать path item object. Вероятно это нужно вторым циклом делать
  // операции только в obj.paths. Вебхуки - только в obj.webhooks. А вот калбеки абы где валяются, там визит
  // и при проходе все ссылки сразу понятно куда. Если относительная - относительно этого файла
  // если локальная - на этот файл (нужно добавлять эту инфу)
  // TODO ещё pathItems в которых переиспользуемые в webhooks / paths операции..
  // крч для pathItems если уж они встречаются - то операция дублируется в webhooks / operations
  // потому что нахер не хочу ссылки городить и неиспользуемая фича максимально
  return result;
}

ir::ir_object spec_loader::generate() {
  ir::ir_object res;
  for (auto& p : loaded_specs) {
    assert(specs.contains(p));
    ir::ir_object ir;
    try {
      ir = visit_and_generate_ir(specs.at(p), p);
    }
    SVARG_ADD_EXCEPTION_INFO(parse_error, p.generic_string());
    res.merge(std::move(ir));
  }
  return res;
}

}  // namespace svarg

// TODO перенести файлы в openapi_frontend
