#include "svarg/spec_loader.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/merge_with_override.hpp"

#include <boost/pfr.hpp>

namespace svarg {

[[nodiscard]] bool is_unnamed(std::string_view jsonptr) {
  // по openapi на все типы вне components/schemas ссылаться нельзя и у них поэтому нет имени
  return !jsonptr.starts_with("/components/schemas");
}

ir::schema schema_lowering(const schema_object& schema, const std::filesystem::path& file,
                           std::string_view jsonptr, std::unordered_set<ir::schema>& schemas,
                           spec_loader& loader);
// TODO
[[nodiscard]] resolved_schema schema_or_ref_to_type_ref(const reference_or<schema_object>& sor,
                                                        const std::filesystem::path& file,
                                                        std::string_view jsonptr, size_t idx,
                                                        std::unordered_set<ir::schema>& schemas,
                                                        spec_loader& loader) {
  if (auto* ref = std::get_if<reference_object>(&sor)) {
    return loader.resolve_ref(*ref, file, schemas);
  } else {
    std::string synthetic = std::format("{}/__inline_schema_{}", jsonptr, idx);
    ir::schema t = schema_lowering(std::get<schema_object>(sor), file, synthetic, schemas, loader);
    return {std::move(t), std::get<schema_object>(sor)};
  }
}

ir::object_schema spec_loader::make_object_type(schema_object const& schema,
                                                const std::filesystem::path& file, std::string_view jsonptr,
                                                std::unordered_set<ir::schema>& schemas) {
  ir::object_schema ot;
  if (schema.properties) {
    size_t i = 0;
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      on_scope_exit {
        ++i;
      };
      // synthetic jsonptr для свойства: jsonptr + "/properties/" + prop_name
      std::string prop_ptr = std::format("{}/properties/{}", jsonptr, prop_name);
      ir::object_field_info fi;
      fi.name = prop_name;
      auto [type, field_schema] = schema_or_ref_to_type_ref(prop_sor, file, prop_ptr, i, schemas, *this);
      fi.type = type.ref();
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
        schema_or_ref_to_type_ref(*schema.additionalProperties, file, jsonptr, 0, schemas, *this)
            .ir_schema.ref();
  }
  return ot;
}

// добавляет встреченные схемы в `schemas`. Как минимум один из schema + инлайн схемы
// loader используется для определения директорий и тд
// file - путь до текущего файла (относительно директории со спеками)
ir::schema schema_lowering(const schema_object& schema, const std::filesystem::path& file,
                           std::string_view jsonptr, std::unordered_set<ir::schema>& schemas,
                           spec_loader& loader) {
  // TODO добавить кеш видимо (скоуп = файл + ссылка => type)
  if (!jsonptr.starts_with('/'))
    throw semantic_error("invalid part of $ref after # (`{}`), must start with /", jsonptr);
  ir::schema res;
  res.scope = std::format("{}#{}", file.generic_string(), jsonptr);
  auto ret_res = [&] {
    schemas.insert(res);
    return std::move(res);
  };
  if (!is_unnamed(jsonptr)) {
    auto pos = jsonptr.find_last_of('/');
    std::string_view name = jsonptr.substr(pos + 1);
    if (name.empty())
      throw semantic_error("name of schema cannot be empty");
    res.name = std::string(name);
  }
  // ── boolean schema ──────────────────────────────────────────────────────────
  if (schema.allow_always()) {
    res.data = ir::any_schema{};
    return ret_res();
  }
  if (schema.allow_never()) {
    res.data = ir::never_schema{};
    return ret_res();
  }

  // ── enum_ / const_ ─────────────────────────────────────────────────────────
  // const_ == enum с одним значением
  if (schema.const_) {
    res.data = ir::enum_schema{.values = {*schema.const_}};
    return ret_res();
  }
  if (schema.enum_) {
    if (schema.enum_->empty())
      throw semantic_error("enum at `{}` must have at least one value", jsonptr);
    res.data = ir::enum_schema{.values = *schema.enum_};
    return ret_res();
  }

  // ── логические комбинаторы ──────────────────────────────────────────────────
  // Проверяем их до type, потому что type может отсутствовать при наличии anyOf и т.п.

  // TODO проверять сначала type. Потому что он важнее, затем уже это просто требование
  if (schema.allOf) {
    ir::allof_schema t;
    // TODO индекс чтобы не зависел от места
    for (size_t i = 0; i < schema.allOf->size(); ++i)
      t.schemas.push_back(
          schema_or_ref_to_type_ref((*schema.allOf)[i], file, jsonptr, i, schemas, loader).ir_schema.ref());
    res.data = std::move(t);
    return ret_res();
  }

  if (schema.anyOf) {
    ir::anyof_schema t;
    t.discriminator = schema.discriminator;
    for (size_t i = 0; i < schema.anyOf->size(); ++i)
      t.schemas.push_back(
          schema_or_ref_to_type_ref((*schema.anyOf)[i], file, jsonptr, i, schemas, loader).ir_schema.ref());
    res.data = std::move(t);
    return ret_res();
  }

  if (schema.oneOf) {
    ir::oneof_schema t;
    t.discriminator = schema.discriminator;
    for (size_t i = 0; i < schema.oneOf->size(); ++i)
      t.schemas.push_back(
          schema_or_ref_to_type_ref((*schema.oneOf)[i], file, jsonptr, i, schemas, loader).ir_schema.ref());
    res.data = std::move(t);
    return ret_res();
  }

  if (schema.not_) {
    ir::not_schema t;
    t.type = schema_or_ref_to_type_ref(*schema.not_, file, jsonptr, 0, schemas, loader).ir_schema.ref();
    res.data = std::move(t);
    return ret_res();
  }

  // ── type: string | array<string> ───────────────────────────────────────────
  if (schema.type) {
    array<string> rtypes = schema.type->values;

    std::sort(rtypes.begin(), rtypes.end());
    rtypes.erase(std::unique(rtypes.begin(), rtypes.end()), rtypes.end());

    if (rtypes.size() == 1) {
      // Единственный тип — прямо в fundamental или object_type
      const auto& t = rtypes[0];

      if (t == "object") {
        res.data = loader.make_object_type(schema, file, jsonptr, schemas);
        return ret_res();
      }

      if (t == "array") {
        ir::array_schema at;
        if (schema.items) {
          at.element =
              schema_or_ref_to_type_ref(*schema.items, file, jsonptr, 0, schemas, loader).ir_schema.ref();
        } else {
          throw parse_error("schema with type 'array' must contain 'items'");
        }
        res.data = std::move(at);
        return ret_res();
      }

      // Всё остальное — фундаментальный тип
      ir::fundamental_e fe;
      efromstr(t, fe);
      res.data = fe;
      return ret_res();
    }

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
      ir::schema_ref ref;
      ref.fundamental = fe;
      anyof.schemas.push_back(std::move(ref));
    }
    res.data = std::move(anyof);
    return ret_res();
  }

  // ── нет type, нет комбинаторов, но есть properties → object ───────────────
  if (schema.properties) {
    res.data = loader.make_object_type(schema, file, jsonptr, schemas);
    return ret_res();
  }

  // ── полностью пустая схема без type → any ──────────────────────────────────
  res.data = ir::any_schema{};
  return ret_res();
}

// TODO эффективнее
static std::string scopes2jsonptr(std::span<const std::string_view> scopes, std::string_view name) {
  std::string s = "/";
  for (std::string_view str : scopes) {
    s += str;
    if (!str.empty())
      s.push_back('/');
  }
  s += name;
  if (s.ends_with('/') && s != "/")
    s.pop_back();
  return s;
}

// ожидает что file относительно spec dir
resolved_schema spec_loader::resolve_ref(reference_object ref, std::filesystem::path file,
                                         std::unordered_set<ir::schema>& schemas) {
  // TODO cache ссылка + файл в тип
  parsed_ref r;
  // поля со всех ссылок складываются сюда. $ref + override FIELD1
  // ссылается на $ref + override FIELD2 и так далее
  string_map<Any> override_fields = std::move(ref._keyfield_override_fields);
  for (size_t recursion_depth = 0; recursion_depth <= opts.ref_max_depth; ++recursion_depth) {
    r = parse_ref(ref.ref_);
    std::filesystem::path refered_file =
        std::filesystem::relative(!r.file.empty() ? file.parent_path() / r.file : file);
    const boost::json::value& v = load_as_json(find_existing_file(refered_file));
    boost::json::value jref = v.at_pointer(r.json_ptr);
    if (!jref.is_object())
      throw semantic_error("refered by $ref must be json/yaml object, but got {}", e2str(jref.kind()));
    for (auto& [k, j] : override_fields)
      jref.as_object()[k] = j.value;
    auto ref_or_schema = parse_from_json<reference_or<schema_object>>(jref);
    if (auto* schema = std::get_if<schema_object>(&ref_or_schema)) {
      return {schema_lowering(*schema, refered_file, r.json_ptr, schemas, *this), std::move(*schema)};
    }
    ref = std::get<reference_object>(std::move(ref_or_schema));
    file = std::move(refered_file);
    override_fields =
        merge_with_override(std::move(override_fields), std::move(ref._keyfield_override_fields));
  }
  throw semantic_error("recursion depth max reached");
}

// TODO хмм.
// а ведь правда это не типы, а схемы
// т.к. иногда они используются просто чтобы проверить соответствие
// и мб опенапи схема распадается на описание того что может лежать и то какие ограничения на это есть
// т.е. в моей логике это всегда чето типа type: string + require schemaA, schemaB, NOT schemaC и так далее
// и вероятно ещё || должно быть
// по сути require A && B && (C || D) это allOf(A, B, anyof(C, D))
// а всё вместе это schemas
// а в моём языке по сути это было бы нечто вроде концептов с && || !
// И TODO? мб моя задача здесь это как раз разбить опенапи схемы на такие части..
// Крч если делать мой язык, то вероятно нужно туда эту концепцию со схемами перенести. дать возможность
// определять схему выкинув всё лишнее и просто с упрощённым синтаксисом
// а ТИП это логическое объединение схем
// так получается

// крч да, нужно просто реализовать опенапи. Всмысле такой IR сделать, чтобы можно было что-то нагенерировать
// первый этап это просто типы и операции без особых ограничений

// оборачивает визитор в ссылке с тем же интерфейсом,
// чтобы копирование результирующей структуры не ломало внутренние данные vtor (н-р если он не умеет
// копироваться) нужно из-за boost::pfr::for_each<***> которые копируют визитор
constexpr auto refvtor(auto& vtor) {
  return [&](std::string_view name, auto&& arg) -> decltype(auto) {
    return vtor(name, std::forward<decltype(arg)>(arg));
  };
}

// посещает все ссылки на `What` и передаёт калбеку json-pointer до элемента и сам элемент
template <typename What>
struct vtor {
  std::filesystem::path obj_file;
  spec_loader* loader;
  std::unordered_set<ir::schema> schemas;
  std::vector<std::string_view> scopes;

  explicit vtor(std::filesystem::path obj_file, spec_loader& l) : obj_file(std::move(obj_file)), loader(&l) {
  }

#define SVARG_ADD_EXCEPTION_INFO(EXTYPE, INFO) \
  catch (std::exception & e) {                 \
    throw EXTYPE("{}/{}", INFO, e.what());     \
  }
  void dbgprint(std::string_view name) {
    // for (std::string_view s : scopes)
    //   std::cout << s << '.';
    // std::cout << name << '\n';
  }
  template <typename T>
  void operator()(std::string_view name, const array<T>& f) try {
    dbgprint(name);
    scopes.push_back(name);
    on_scope_exit {
      scopes.pop_back();
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
    scopes.push_back(name);
    on_scope_exit {
      scopes.pop_back();
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
    scopes.push_back(name);
    dbgprint(name);
    on_scope_exit {
      scopes.pop_back();
    };
    boost::pfr::for_each_field_with_name(x, refvtor(*this));
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  void operator()(std::string_view name, const schema_object& x) try {
    dbgprint(name);
    schema_lowering(x, obj_file, scopes2jsonptr(scopes, name), schemas, *loader);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  template <typename T>
  void operator()(std::string_view name, const reference_or<T>& x) {
    dbgprint(name);

    if (auto* r = std::get_if<reference_object>(&x)) {
      // TODO тут явно надо подумать что с другими делать (ссылки на параметры и тд)
      if constexpr (!std::is_same_v<T, What>)
        return;
      loader->resolve_ref(*r, obj_file, schemas);
    } else {
      (*this)(name, *std::get_if<T>(&x));
    }
  }

  template <typename T>
  void operator()(std::string_view name, const optional<T>& x) {
    dbgprint(name);
    if (x)
      (*this)(name, *x);
  }
};

ir::ir_object spec_loader::visit_and_generate_ir(const openapi_object& obj,
                                                 const std::filesystem::path& obj_file) {
  vtor<schema_object> v(obj_file, *this);
  boost::pfr::for_each_field_with_name(obj, refvtor(v));
  // TODO надо пройти по операциями, калбекам и вебхукам
  // и конвертировать их в IR. Операции - в operation, калбеки и вебхуки в webhooks
  // т.е. по сути нужно посещать path item object. Вероятно это нужно вторым циклом делать
  // операции только в obj.paths. Вебхуки - только в obj.webhooks. А вот калбеки абы где валяются, там визит
  // и при проходе все ссылки сразу понятно куда. Если относительная - относительно этого файла
  // если локальная - на этот файл (нужно добавлять эту инфу)
  // TODO ещё pathItems в которых переиспользуемые в webhooks / paths операции..
  // крч для pathItems если уж они встречаются - то операция дублируется в webhooks / operations
  // потому что нахер не хочу ссылки городить и неиспользуемая фича максимально
  return ir::ir_object{.schemas = std::move(v.schemas)};
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
