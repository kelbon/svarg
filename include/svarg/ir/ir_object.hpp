#pragma once

#include <boost/json/serialize.hpp>
#include <boost/container_hash/hash.hpp>
#include <set>  // используется для стабильности вывода вместо unordered_set

#include "svarg/utils/algo.hpp"
#include "svarg/utils/forward_like.hpp"
#include "svarg/utils/path_tools.hpp"
#include "svarg/utils/json_pointer.hpp"
#include "svarg/utils/strcmp.hpp"
#include "svarg/utils/combined_hash.hpp"
#include "svarg/utils/from_chars.hpp"
#include "svarg/utils/matcher.hpp"

namespace svarg::ir {

struct schema;

enum struct fundamental_e {
  null,
  boolean,
  integer,
  number,
  string,
  any,    // type: true
  never,  // type: never
};

[[nodiscard]] inline bool efromstr(std::string_view s, fundamental_e& out) {
  using enum fundamental_e;
  std::optional loc = ss::string_switch<fundamental_e>(s)
                          .case_("null", null)
                          .case_("boolean", boolean)
                          .case_("integer", integer)
                          .case_("number", number)
                          .case_("string", string)
                          .case_("any", any)
                          .case_("never", never)
                          .or_nullopt();
  if (!loc)
    return false;
  out = *loc;
  return true;
}

inline std::string_view e2str(fundamental_e e) noexcept {
  switch (e) {
    case fundamental_e::null:
      return "null";
    case fundamental_e::boolean:
      return "boolean";
    case fundamental_e::integer:
      return "integer";
    case fundamental_e::number:
      return "number";
    case fundamental_e::string:
      return "string";
    case fundamental_e::any:
      return "any";
    case fundamental_e::never:
      return "never";
  }
  std::unreachable();
}

struct schema_ref {
  SVARG_IR_TYPE;

  std::optional<string> name;
  // .filename() спеки где объявлена схема без расширения ИЛИ ссылка на фундаментал (типа @string@)
  string spec;
  // путь в спеке (не обязательно он существует, в случае $ref + перекрытое поле может сгенерироваться)
  // не указано, если ссылка на фундаментальный тип
  std::optional<json_pointer> jsonptr;

  [[nodiscard]] bool refers_to(const schema&) const noexcept;

  [[nodiscard]] std::optional<fundamental_e> if_fundamental() const noexcept {
    using enum fundamental_e;
    return ss::string_switch<fundamental_e>(spec)
        .case_("@null@", null)
        .case_("@boolean@", boolean)
        .case_("@integer@", integer)
        .case_("@number@", number)
        .case_("@string@", string)
        .case_("@any@", any)
        .case_("@never@", never)
        .or_nullopt();
  }

  // возвращает true если ссылается на фундаментал
  [[nodiscard]] bool is_fundamental() const noexcept {
    return if_fundamental() != std::nullopt;
  }
  [[nodiscard]] bool is_fundamental(fundamental_e f) const noexcept {
    return if_fundamental() == f;
  }

  bool operator==(const schema_ref& r) const noexcept {
    return operator<=>(r) == std::strong_ordering::equal;
  }
  std::strong_ordering operator<=>(const schema_ref& r) const noexcept {
    return std::tie(spec, jsonptr, name) <=> std::tie(r.spec, r.jsonptr, name);
  }
};

[[nodiscard]] inline size_t hash_schema(std::string_view spec, const json_pointer* jp) {
  combined_hash h;
  h.add(spec);
  if (jp)
    h.add(jp->escaped());
  return h;
}

inline schema_ref make_fundamental_ref(fundamental_e e) noexcept {
  schema_ref ref;
  auto f2scope = [&]() -> std::string_view {
    switch (e) {
      case fundamental_e::null:
        return "@null@";
      case fundamental_e::boolean:
        return "@boolean@";
      case fundamental_e::integer:
        return "@integer@";
      case fundamental_e::number:
        return "@number@";
      case fundamental_e::string:
        return "@string@";
      case fundamental_e::any:
        return "@any@";
      case fundamental_e::never:
        return "@never@";
    }
  };
  ref.spec = f2scope();
  return ref;
}

struct enum_schema {
  SVARG_IR_TYPE;

  schema_ref type;
  array<Any> values;
};

struct discriminator_desc {
  SVARG_IR_TYPE;

  string propertyName;
  // при конвертации с фронтенда здесь оказывается полный маппинг
  // значение дискриминатора => ссылка на тип который ему соответствует
  string_map<schema_ref> mapping;
};

struct oneof_schema {
  SVARG_IR_TYPE;

  std::optional<discriminator_desc> discriminator;
  array<schema_ref> schemas;
};

struct anyof_schema {
  SVARG_IR_TYPE;

  std::optional<discriminator_desc> discriminator;
  array<schema_ref> schemas;
};

struct allof_schema {
  SVARG_IR_TYPE;

  array<schema_ref> schemas;
};

struct object_field_info {
  SVARG_IR_TYPE;

  schema_ref type;
  string name;
  // поле считается required если
  // * указано в required: [...]
  // * тип НЕ nullable
  // * у типа НЕТ default
  // * тип НЕ readOnly/writeOnly (там необходимость передавать зависит от запрос/ответ, но тип то один)
  // if/then/else игнорируются
  bool required = false;

  std::strong_ordering operator<=>(const object_field_info& r) const noexcept {
    return type <=> r.type;
  }
};

struct object_schema {
  SVARG_IR_TYPE;

  array<object_field_info> fields;
  std::optional<schema_ref> additional_properties;
};

struct array_schema {
  SVARG_IR_TYPE;

  schema_ref element;
};

struct not_schema {
  SVARG_IR_TYPE;

  schema_ref type;
};

struct schema_type {
  SVARG_IR_TYPE;

  std::variant<fundamental_e, enum_schema, oneof_schema, anyof_schema, allof_schema, object_schema,
               array_schema, not_schema>
      data;

  bool is_fundamental() const noexcept {
    return std::holds_alternative<fundamental_e>(data);
  }
  bool is_enum() const noexcept {
    return std::holds_alternative<enum_schema>(data);
  }
  bool is_oneof() const noexcept {
    return std::holds_alternative<oneof_schema>(data);
  }
  bool is_anyof() const noexcept {
    return std::holds_alternative<anyof_schema>(data);
  }
  bool is_allof() const noexcept {
    return std::holds_alternative<allof_schema>(data);
  }
  bool is_object() const noexcept {
    return std::holds_alternative<object_schema>(data);
  }
  bool is_array() const noexcept {
    return std::holds_alternative<array_schema>(data);
  }
  bool is_not() const noexcept {
    return std::holds_alternative<not_schema>(data);
  }

  auto* if_fundamental() noexcept {
    return std::get_if<fundamental_e>(&data);
  }
  auto* if_enum() noexcept {
    return std::get_if<enum_schema>(&data);
  }
  auto* if_oneof() noexcept {
    return std::get_if<oneof_schema>(&data);
  }
  auto* if_anyof() noexcept {
    return std::get_if<anyof_schema>(&data);
  }
  auto* if_allof() noexcept {
    return std::get_if<allof_schema>(&data);
  }
  auto* if_object() noexcept {
    return std::get_if<object_schema>(&data);
  }
  auto* if_array() noexcept {
    return std::get_if<array_schema>(&data);
  }
  auto* if_not() noexcept {
    return std::get_if<not_schema>(&data);
  }

  auto* if_fundamental() const noexcept {
    return std::get_if<fundamental_e>(&data);
  }
  auto* if_enum() const noexcept {
    return std::get_if<enum_schema>(&data);
  }
  auto* if_oneof() const noexcept {
    return std::get_if<oneof_schema>(&data);
  }
  auto* if_anyof() const noexcept {
    return std::get_if<anyof_schema>(&data);
  }
  auto* if_allof() const noexcept {
    return std::get_if<allof_schema>(&data);
  }
  auto* if_object() const noexcept {
    return std::get_if<object_schema>(&data);
  }
  auto* if_array() const noexcept {
    return std::get_if<array_schema>(&data);
  }
  auto* if_not() const noexcept {
    return std::get_if<not_schema>(&data);
  }

  std::string_view discriminator() const noexcept {
    return std::visit(
        matcher_r<std::string_view>(
            [](const not_schema&) { return "not"; }, [](const array_schema&) { return "array"; },
            [](const object_schema&) { return "object"; }, [](const allof_schema&) { return "allOf"; },
            [](const anyof_schema&) { return "anyOf"; }, [](const oneof_schema&) { return "oneOf"; },
            [](const enum_schema&) { return "enum"; }, [](const fundamental_e&) { return "fundamental"; }),
        data);
  }
  void set_type_from_discriminator(std::string_view s) {
    if (s == "not")
      data.emplace<not_schema>();
    else if (s == "array")
      data.emplace<array_schema>();
    else if (s == "object")
      data.emplace<object_schema>();
    else if (s == "allOf")
      data.emplace<allof_schema>();
    else if (s == "anyOf")
      data.emplace<anyof_schema>();
    else if (s == "oneOf")
      data.emplace<oneof_schema>();
    else if (s == "enum")
      data.emplace<enum_schema>();
    else if (s == "fundamental")
      data.emplace<fundamental_e>();
    else
      throw parse_error("invalid discriminator `{}`", s);
  }
};

// fundamental | enum | object_type | oneof | anyof | allof
struct schema {
  SVARG_IR_TYPE;

  // .filename() спеки где написана схема без расширения
  string spec;
  json_pointer jsonptr;

  // всё что ниже не влияет на хеш/сравнение

  // если имени нет это инлайн схема
  std::optional<string> name;
  // представление типа (то что получит пользователь генератора)
  schema_type type;
  // схемы которые не влияют на представление, только на валидацию
  // н-р type: string enum: ... это означает что пользователь увидит строку, но она провалидируется по тому
  // что написано в enum
  std::optional<array<schema_ref>> required;

  std::string scope() const {
    return std::format("{}#{}", spec, jsonptr.escaped());
  }

  bool has_requirements() const noexcept {
    return required && !required->empty();
  }

  void add_requirements(const array<schema_ref>& reqs) {
    if (!required)
      required.emplace();
    append_range(*required, reqs);
  }
  void add_requirement(schema_ref s) {
    if (!required)
      required.emplace();
    required->push_back(std::move(s));
  }

  template <typename Me>
  schema_ref ref(this Me&& self) {
    schema_ref r;
    if (self.name)
      r.name = noadl::forwardLike<Me>(*self.name);
    r.spec = noadl::forwardLike<Me>(self.spec);
    r.jsonptr = noadl::forwardLike<Me>(self.jsonptr);
    return r;
  }

  bool is_trivial() const noexcept {
    return !has_requirements() && std::holds_alternative<fundamental_e>(type.data);
  }

  bool operator==(const schema& x) const noexcept {
    return spec == x.spec && jsonptr == x.jsonptr && name == x.name;
  }
  std::strong_ordering operator<=>(const schema& x) const noexcept {
    return std::tie(spec, jsonptr, name) <=> std::tie(x.spec, x.jsonptr, x.name);
  }

  struct schema_cmp {
    using is_transparent = int;

    bool operator()(const schema& a, const schema& b) const {
      return a < b;
    }
    bool operator()(const schema& a, const schema_ref& b) const {
      return std::tie(a.spec, a.jsonptr, a.name) < std::tie(b.spec, b.jsonptr, b.name);
    }
    bool operator()(const schema_ref& a, const schema& b) const {
      return std::tie(a.spec, a.jsonptr, a.name) < std::tie(b.spec, b.jsonptr, b.name);
    }
    bool operator()(const schema_ref& a, const schema_ref& b) const {
      return std::tie(a.spec, a.jsonptr, a.name) < std::tie(b.spec, b.jsonptr, b.name);
    }
  };
};

[[nodiscard]] inline bool is_meta_key(std::string_view key) noexcept {
  return key.starts_with("x-");
}

// все ключи должны быть is_meta_key(key)
// хранилище разных вспомогательных тегов, которые фронтенд хочет прокинуть бекенду
// н-р deprecated или формат query и тд
using meta_t = string_map<Any>;

struct content_type_t {
  SVARG_IR_TYPE;

 private:
  std::string data;

 public:
  content_type_t() = default;

  // может быть типом контента или ренжом
  content_type_t(std::convertible_to<std::string_view> auto&& in)
      : data(monomorph_media_type(without_media_type_parameters(std::string_view(in)))) {
  }

  content_type_t& operator=(std::convertible_to<std::string_view> auto&& str) {
    std::destroy_at(this);
    return *std::construct_at(this, std::string_view(str));
  }

  std::string_view str() const noexcept {
    return data;
  }
  operator std::string_view() const noexcept {
    return str();
  }

  std::strong_ordering operator<=>(const content_type_t& r) const {
    return compare_media_types(str(), r.str());
  }
};

struct operation_parameter {
  SVARG_IR_TYPE;

  parameter_location location = parameter_location::path;
  string name;
  schema_ref schema;
  // по умолчанию false
  std::optional<bool> required;
  // аналогично упрощённому encoding из openapi
  // говорит как отправлять этот параметр, н-р application/json
  // по умолчанию такой же как у тела запроса
  std::optional<content_type_t> encoding;
  std::optional<meta_t> meta;

  bool operator==(const operation_parameter& r) const noexcept {
    return location == r.location && name == r.name;
  }
  std::strong_ordering operator<=>(const operation_parameter& r) const noexcept {
    return std::tie(location, name) <=> std::tie(r.location, r.name);
  }
};

struct header {
  SVARG_IR_TYPE;

  // не зависит от регистра
  string name;
  // по умолчанию false
  std::optional<bool> required;
  // по умолчанию эквивалентно type: string
  std::optional<schema_ref> schema;

  bool operator==(const header& r) const noexcept {
    return is_equal_ci(name, r.name);
  }
  std::strong_ordering operator<=>(const header& r) const noexcept {
    return compare_ci(name, r.name);
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::header> {
  size_t operator()(const ::svarg::ir::header& h) const {
    svarg::combined_hash hash;
    // без учёта регистра
    for (char c : h.name)
      hash.add(svarg::to_lower_ascii(c));
    return hash;
  }
};

template <>
struct hash<::svarg::ir::operation_parameter> {
  size_t operator()(const ::svarg::ir::operation_parameter& p) const {
    return ::svarg::hash_combined(std::to_underlying(p.location), p.name);
  }
};

}  // namespace std

namespace svarg::ir {

struct body {
  SVARG_IR_TYPE;

  // может быть также media type range */* - любой, image/* (image/x* уже невалидно)
  // выигрывает более специализированный
  content_type_t contentType;
  schema_ref schema;
  // мапа имя property из schema => как его кодировать конкретно в этом теле
  // application/json и тд контент типы
  std::optional<string_map<content_type_t>> membersEncoding;
  // означает, что schema применяется не ко всему телу, а к единице стрима
  // (например openapi 3.2 itemsSchema)
  // по умолчанию false
  std::optional<bool> streaming;

  bool operator==(const body& r) const noexcept {
    return operator<=>(r) == std::strong_ordering::equal;
  }
  std::strong_ordering operator<=>(const body& r) const noexcept {
    return contentType <=> r.contentType;
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::body> {
  size_t operator()(const ::svarg::ir::body& b) const noexcept {
    return ::svarg::hash_combined(b.contentType.str());
  }
};

template <>
struct hash<::svarg::ir::schema_ref> {
  size_t operator()(const ::svarg::ir::schema_ref& r) const noexcept {
    return ::svarg::ir::hash_schema(r.spec, r.jsonptr ? &*r.jsonptr : nullptr);
  }
};

}  // namespace std

namespace svarg::ir {

struct default_response_t {
  auto operator<=>(const default_response_t&) const = default;
};

// тег тип для сериализации
struct response_status {
  SVARG_IR_TYPE;

  std::variant<default_response_t, int64_t, status_range_e> status;

  std::string to_string() const noexcept {
    return std::visit(matcher_r<std::string>([](default_response_t) { return "default"; },
                                             [](int64_t s) { return std::to_string(s); },
                                             [](status_range_e e) { return e2str(e); }),
                      status);
  }

  [[nodiscard]] static response_status from_string(std::string_view str) {
    response_status result;
    if (str == "default") {
      result.status.emplace<default_response_t>();
      return result;
    }
    using enum status_range_e;
    std::optional sr = ss::string_switch<status_range_e>(str)
                           .case_("1XX", _1XX)
                           .case_("2XX", _2XX)
                           .case_("3XX", _3XX)
                           .case_("4XX", _4XX)
                           .case_("5XX", _5XX)
                           .or_nullopt();
    if (sr) {
      result.status.emplace<status_range_e>(*sr);
      return result;
    }
    if (!try_from_chars(str, result.status.emplace<int64_t>()))
      throw parse_error("invalid status string `{}`", str);
    return result;
  }

  auto operator<=>(const response_status&) const = default;
};

struct response_desc {
  SVARG_IR_TYPE;

  response_status status;
  // content-type игнорируется (даже не попадает в этот список)
  // регистронезависимо
  std::optional<std::set<header>> headers;
  // на каждый contentType своё тело
  std::optional<std::set<body>> bodies;

  [[nodiscard]] bool is_default() const noexcept {
    return std::holds_alternative<default_response_t>(status.status);
  }
  [[nodiscard]] bool is_range() const noexcept {
    return std::holds_alternative<status_range_e>(status.status);
  }

  void make_default() noexcept {
    status.status.emplace<default_response_t>();
  }
  void set_status(std::string_view s) {
    status = status.from_string(s);
  }

  bool operator==(const response_desc& r) const noexcept {
    return status == r.status;
  }
  std::strong_ordering operator<=>(const response_desc& r) const noexcept {
    return status <=> r.status;
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::response_desc> {
  size_t operator()(const ::svarg::ir::response_desc& o) const {
    return svarg::hash_combined(o.status.to_string());
  }
};

}  // namespace std

namespace svarg::ir {

struct operation {
  SVARG_IR_TYPE;

  // operationId, а если нет, используется method + путь с большой буквы
  string id;
  // .filename() спеки где объявлена операция без расширения
  string spec;
  // путь относительно url сервера. Должен начинаться с / (если не пустой)
  // path + method + spec должны быть уникальны. Шаблонизированность не учитывается
  // если это callback/webhook, то путь пуст
  // TODO генерировать проверки на фронтенде и на бекенде надо делать парсеры путей
  // н-р если users/{id}, то обязательно должен быть параметр с in: path и именем id
  // TODO использовать правила валидации из openapie 3.2
  // https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.2.0.md#path-templating
  string path;
  // get/post и тд. Всегда маленькие буквы
  // строкой для поддержки кастомных методов (openapi 3.2)
  string method;
  std::optional<array<operation_parameter>> parameters;
  // на каждый contentType своё тело
  std::optional<std::set<body>> bodies;
  std::optional<std::set<response_desc>> responses;
  std::optional<response_desc> defaultResponse;
  std::optional<meta_t> meta;

  bool operator==(const operation& r) const noexcept {
    // имя не участвует в сравнении, так как при отправлении на тот же путь с тем же методом
    // неважно как оно называется (при этом из разных спек это всё же разные операции)
    return operator<=>(r) == std::strong_ordering::equal;
  }
  std::strong_ordering operator<=>(const operation& r) const noexcept {
    std::strong_ordering res = std::tie(spec, method) <=> std::tie(r.spec, r.method);
    if (res != std::strong_ordering::equal)
      return res;
    return compare_paths_template_insensitve(path, r.path);
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::schema> {
  size_t operator()(const ::svarg::ir::schema& t) const {
    return ::svarg::ir::hash_schema(t.spec, &t.jsonptr);
  }
};

template <>
struct hash<::svarg::ir::operation> {
  size_t operator()(const ::svarg::ir::operation& o) const {
    svarg::combined_hash hash;
    hash.add(o.spec, o.method);
    ::svarg::visit_path_segments(o.path, [&](std::string_view segment) {
      // игнорируем сломанные сегменты типа {{abc} и так далее
      if (segment.starts_with('{')) {
        // игнорируем шаблонизацию, users/{id} == users/{name}
        hash.add("{}");
      } else {
        hash.add(segment);
      }
    });
    return hash;
  }
};

}  // namespace std

namespace svarg::ir {

using schemas_t = std::set<schema, schema::schema_cmp>;
using schema_refs_t = std::set<schema_ref, schema::schema_cmp>;

struct ir_object {
  SVARG_IR_TYPE;

  std::optional<schemas_t> schemas;
  std::optional<std::set<operation>> operations;
  // здесь помимо webhooks лежат callbacks (они то же самое, но ещё вычислимый url)
  std::optional<std::set<operation>> webhooks;
  // TODO servers

  // объединяет 2 ir в один
  void merge(ir_object obj);

  void validate() const;

  bool operator==(const ir_object&) const = default;
};

[[nodiscard]] bool validate_ir_object(const ir_object& ir);

}  // namespace svarg::ir

namespace svarg {

template <>
struct from_json_value<ir::content_type_t> {
  error_code operator()(const boost::json::value& v, ir::content_type_t& out) {
    out = std::string_view(v.as_string());
    return {};
  }
};

template <>
struct from_json_value<ir::response_status> {
  error_code operator()(const boost::json::value& v, ir::response_status& out) {
    // может в кривой ситуации бросить исключение, но это игнорируется
    out = ir::response_status::from_string(v.as_string());
    return {};
  }
};

template <>
struct from_json_value<ir::schema_type> {
  error_code operator()(const boost::json::value& v, ir::schema_type& out) {
    if (!v.is_object())
      return parse_error(" object expected, got `{}`", e2str(v.kind()));
    const boost::json::object& obj = v.as_object();
    if (obj.size() != 1)
      return parse_error(" expected exactly one key, but `{}` keys present", obj.size());
    std::string_view discriminator = obj.begin()->key();
    out.set_type_from_discriminator(discriminator);
    return std::visit([&](auto& x) { return try_from_json(obj.begin()->value(), x); }, out.data);
  }
};

template <>
struct serialize<ir::content_type_t> {
  boost::json::value operator()(const ir::content_type_t& r) {
    return boost::json::string(r.str());
  }
};

template <>
struct serialize<ir::response_status> {
  boost::json::value operator()(const ir::response_status& r) {
    return boost::json::string(r.to_string());
  }
};

template <>
struct serialize<ir::schema_type> {
  boost::json::value operator()(const ir::schema_type& s) {
    boost::json::object obj;
    obj[s.discriminator()] = std::visit([](auto& x) { return to_json(x); }, s.data);
    return obj;
  }
};

}  // namespace svarg
