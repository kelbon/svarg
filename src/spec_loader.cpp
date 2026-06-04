#include "svarg/spec_loader.hpp"
#include "svarg/utils/json_pretty.hpp"
#include "svarg/utils/matcher.hpp"
#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/merge_with_override.hpp"

#include <boost/pfr.hpp>

namespace svarg {

[[nodiscard]] bool is_unnamed(std::string_view jsonptr) {
  // по openapi на все типы вне components/schemas ссылаться нельзя и у них поэтому нет имени
  return !jsonptr.starts_with("/components/schemas");
}

[[nodiscard]] ir::type_ref make_ref_for(const std::filesystem::path& file, std::string_view jsonptr) {
  ir::type_ref ref;
  ref.scope = std::format("{}#{}", file.generic_string(), jsonptr);
  if (!is_unnamed(jsonptr)) {
    auto pos = jsonptr.find_last_of('/');
    std::string_view name = jsonptr.substr(pos + 1);
    if (name.empty())
      throw semantic_error("name cannot be empty, err in {}#{}", file.generic_string(), jsonptr);
    ref.name = std::string(name);
  }
  return ref;
}

void frontend_schema2ir_type(const schema_object& schema, const std::filesystem::path& file,
                             std::string_view jsonptr, std::vector<ir::type>& types, spec_loader& loader);

[[nodiscard]] ir::type_ref schema_or_ref_to_type_ref(const reference_or<schema_object>& sor,
                                                     const std::filesystem::path& file,
                                                     std::string_view jsonptr, size_t idx,
                                                     std::vector<ir::type>& types, spec_loader& loader) {
  if (auto* ref = std::get_if<reference_object>(&sor)) {
    auto pref = parse_ref(ref->ref_);
    std::filesystem::path searching = pref.file.empty() ? file : file.parent_path() / pref.file;
    std::filesystem::path p = loader.find_existing_file(std::move(searching));
    return make_ref_for(p, pref.json_ptr);
  } else {
    std::string synthetic = std::format("{}/__inline_schema_{}", jsonptr, idx);
    ir::type_ref res = make_ref_for(file, synthetic);
    for (auto& t : types)
      if (res.refers_to(t))
        return res;
    frontend_schema2ir_type(std::get<schema_object>(sor), file, synthetic, types, loader);
    return res;
  }
}

// добавляет встреченные типы в `types`. Как минимум один из schema + инлайн схемы
// loader используется для определения директорий и тд
// file - путь до текущего файла (относительно директории со спеками)
void frontend_schema2ir_type(const schema_object& schema, const std::filesystem::path& file,
                             std::string_view jsonptr, std::vector<ir::type>& types, spec_loader& loader) {
  if (!jsonptr.starts_with('/'))
    throw semantic_error("invalid part of $ref after # (`{}`), must start with /", jsonptr);
  ir::type res;
  res.scope = std::format("{}#{}", file.generic_string(), jsonptr);
  auto ret_res = [&] {
    // TODO O(1)
    if (!std::ranges::contains(types, res))
      types.push_back(std::move(res));
  };
  if (!is_unnamed(jsonptr)) {
    auto pos = jsonptr.find_last_of('/');
    std::string_view name = jsonptr.substr(pos + 1);
    if (name.empty())
      throw semantic_error("name of type cannot be empty");
    res.name = std::string(name);
  }
  // ── boolean schema ──────────────────────────────────────────────────────────
  if (schema.allow_always()) {
    res.data = ir::any_type{};
    ret_res();
    return;
  }
  if (schema.allow_never()) {
    res.data = ir::never_type{};
    ret_res();
    return;
  }

  // ── enum_ / const_ ─────────────────────────────────────────────────────────
  // const_ == enum с одним значением
  if (schema.const_) {
    res.data = ir::enum_type{.values = {*schema.const_}};
    ret_res();
    return;
  }
  if (schema.enum_) {
    if (schema.enum_->empty())
      throw semantic_error("enum at `{}` must have at least one value", jsonptr);
    res.data = ir::enum_type{.values = *schema.enum_};
    ret_res();
    return;
  }

  // ── логические комбинаторы ──────────────────────────────────────────────────
  // Проверяем их до type, потому что type может отсутствовать при наличии anyOf и т.п.

  if (schema.allOf) {
    ir::allof_type t;
    // TODO индекс чтобы не зависел от места
    for (size_t i = 0; i < schema.allOf->size(); ++i)
      t.types.push_back(schema_or_ref_to_type_ref((*schema.allOf)[i], file, jsonptr, i, types, loader));
    res.data = std::move(t);
    ret_res();
    return;
  }

  if (schema.anyOf) {
    ir::anyof_type t;
    t.discriminator = schema.discriminator;
    for (size_t i = 0; i < schema.anyOf->size(); ++i)
      t.types.push_back(schema_or_ref_to_type_ref((*schema.anyOf)[i], file, jsonptr, i, types, loader));
    res.data = std::move(t);
    ret_res();
    return;
  }

  if (schema.oneOf) {
    ir::oneof_type t;
    t.discriminator = schema.discriminator;
    for (size_t i = 0; i < schema.oneOf->size(); ++i)
      t.types.push_back(schema_or_ref_to_type_ref((*schema.oneOf)[i], file, jsonptr, i, types, loader));
    res.data = std::move(t);
    ret_res();
    return;
  }

  if (schema.not_) {
    ir::not_type t;
    t.type = schema_or_ref_to_type_ref(*schema.not_, file, jsonptr, 0, types, loader);
    res.data = std::move(t);
    ret_res();
    return;
  }

  // ── type: string | array<string> ───────────────────────────────────────────
  if (schema.type) {
    // Нормализуем к массиву
    array<string> rtypes = schema.type->values;

    std::sort(rtypes.begin(), rtypes.end());
    rtypes.erase(std::unique(rtypes.begin(), rtypes.end()), rtypes.end());

    if (rtypes.size() == 1) {
      // Единственный тип — прямо в fundamental или object_type
      const auto& t = rtypes[0];

      if (t == "object") {
        ir::object_type ot;
        if (schema.properties) {
          size_t i = 0;
          for (auto& [prop_name, prop_sor] : *schema.properties) {
            on_scope_exit {
              ++i;
            };
            // synthetic jsonptr для свойства: jsonptr + "/properties/" + prop_name
            auto prop_ptr = std::format("{}/properties/{}", jsonptr, prop_name);
            ir::type_ref ref;
            if (std::holds_alternative<reference_object>(prop_sor)) {
              ref = schema_or_ref_to_type_ref(prop_sor, file, prop_ptr, i, types, loader);
            } else {
              ref.name = prop_name;
              // scope — путь до properties/
              ref.scope = std::format("{}/properties/", jsonptr);
            }
            ot.fields.push_back(std::move(ref));
          }
        }
        if (schema.additionalProperties) {
          ot.additional_fields =
              schema_or_ref_to_type_ref(*schema.additionalProperties, file, jsonptr, 0, types, loader);
        }
        res.data = std::move(ot);
        ret_res();
        return;
      }

      if (t == "array") {
        ir::array_type at;
        if (schema.items) {
          at.element = schema_or_ref_to_type_ref(*schema.items, file, jsonptr, 0, types, loader);
        } else {
          throw parse_error("schema with type 'array' must contain 'items'");
        }
        res.data = std::move(at);
        ret_res();
        return;
      }

      // Всё остальное — фундаментальный тип
      ir::fundamental_e fe;
      efromstr(t, fe);
      res.data = fe;
      ret_res();
      return;
    }

    // Несколько типов → anyOf из фундаментальных type_ref-ов
    // Например: ["string", "null"] или ["integer", "string"]
    ir::anyof_type anyof;
    for (auto& t : rtypes) {
      ir::fundamental_e fe;
      efromstr(t, fe);
      if (fe == ir::fundamental_e::object || fe == ir::fundamental_e::array) {
        throw semantic_error(
            "field type with several types, where one of them object or array is not supported");
      }
      ir::type_ref ref;
      ref.fundamental = fe;
      anyof.types.push_back(std::move(ref));
    }
    res.data = std::move(anyof);
    ret_res();
    return;
  }

  // ── нет type, нет комбинаторов, но есть properties → object ───────────────
  if (schema.properties) {
    ir::object_type ot;
    for (auto& [prop_name, prop_sor] : *schema.properties) {
      ir::type_ref ref;
      ref.name = prop_name;
      ref.scope = std::format("{}/properties/", jsonptr);
      ot.fields.push_back(std::move(ref));
    }
    if (schema.additionalProperties)
      ot.additional_fields =
          schema_or_ref_to_type_ref(*schema.additionalProperties, file, jsonptr, 0, types, loader);
    res.data = std::move(ot);
    ret_res();
    return;
  }

  // ── полностью пустая схема без type → any ──────────────────────────────────
  res.data = ir::any_type{};
  ret_res();
  return;
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
template <typename T>
void spec_loader::resolve_ref_to(reference_object ref, std::filesystem::path file,
                                 std::vector<ir::type>& types) {
  parsed_ref r;
  // поля со всех ссылок складываются сюда. $ref + override FIELD1
  // ссылается на $ref + override FIELD2 и так далее
  string_map<Any> override_fields = std::move(ref._keyfield_override_fields);
  for (size_t recursion_depth = 0; recursion_depth <= opts.ref_max_depth; ++recursion_depth) {
    r = parse_ref(ref.ref_);
    std::filesystem::path refered_file = !r.file.empty()
                                             ? std::filesystem::canonical(file).parent_path() / r.file
                                             : std::filesystem::canonical(file);
    const boost::json::value& v = jcache.load(refered_file);
    boost::json::value jref = v.at_pointer(r.json_ptr);
    if (!jref.is_object())
      throw semantic_error("refered by $ref must be json/yaml object, but got {}", e2str(jref.kind()));
    for (auto& [k, j] : override_fields)
      jref.as_object()[k] = j.value;
    auto ref_or_schema = parse_from_json<reference_or<schema_object>>(jref);
    if (auto* s = std::get_if<schema_object>(&ref_or_schema)) {
      return frontend_schema2ir_type(*s, std::filesystem::relative(refered_file, specs_dir), r.json_ptr,
                                     types, *this);
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
  std::vector<ir::type> types;
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
    // TODO O(1)
    frontend_schema2ir_type(x, obj_file, scopes2jsonptr(scopes, name), types, *loader);
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

  template <typename T>
  void operator()(std::string_view name, const reference_or<T>& x) try {
    dbgprint(name);

    if (auto* r = std::get_if<reference_object>(&x)) {
      if constexpr (!std::is_same_v<T, What>)
        return;
      loader->resolve_ref_to<What>(*r, obj_file, types);
    } else {
      (*this)(name, *std::get_if<T>(&x));
    }
  }
  SVARG_ADD_EXCEPTION_INFO(parse_error, name);

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
  return ir::ir_object{.types = std::move(v.types)};
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
