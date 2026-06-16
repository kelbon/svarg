#pragma once

#include <boost/json/serialize.hpp>
#include <boost/container_hash/hash.hpp>
#include <unordered_set>

#include "svarg/utils/forward_like.hpp"
#include "svarg/utils/path_tools.hpp"
#include "svarg/utils/json_pointer.hpp"
#include "svarg/utils/strcmp.hpp"
#include "svarg/utils/combined_hash.hpp"
#include "svarg/utils/from_chars.hpp"

namespace svarg::ir {

struct schema;

enum struct fundamental_e {
  null,
  boolean,
  integer,
  number,
  string,
  array,
  object,
};

inline void efromstr(std::string_view s, fundamental_e& out) {
  using enum fundamental_e;
  auto loc = ss::string_switch<fundamental_e>(s)
                 .case_("null", null)
                 .case_("boolean", boolean)
                 .case_("integer", integer)
                 .case_("number", number)
                 .case_("string", string)
                 .case_("array", array)
                 .case_("object", object)
                 .or_default(fundamental_e(-1));
  if (loc == fundamental_e(-1))
    throw parse_error("unknown fundamental type `{}`", s);
  out = loc;
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
    case fundamental_e::array:
      return "array";
    case fundamental_e::object:
      return "object";
  }
}

struct schema_ref {
  std::optional<string> name;
  // может быть ссылкой на фундаментал (@string@ и тд)
  // generic путь до спеки
  string spec;
  // путь в спеке (не обязательно он существует, в случае $ref + перекрытое поле может сгенерироваться)
  json_pointer jsonptr;
  // TODO тест с refers_to фундаментальные типы и тд
  [[nodiscard]] bool refers_to(const schema&) const noexcept;
};

[[nodiscard]] inline size_t hash_schema(std::string_view spec, const json_pointer& jp) {
  return hash_combined(spec, jp.escaped());
}

// пытается распарсить ссылку на фундаментал
// (у меня используются "ссылки" на фундаментальные типы для удобства реализации)
// таких ссылок нет и не может быть в openapi yaml, они есть только на уровне IR
inline std::optional<fundamental_e> try_parse_as_fundamental_ref(std::string_view ref) {
  // подчёркивание в начале чтобы избежать ошибки и принять это за локальную ссылку
  return ss::string_switch<std::optional<fundamental_e>>(ref)
      .case_("@null@", fundamental_e::null)
      .case_("@boolean@", fundamental_e::boolean)
      .case_("@integer@", fundamental_e::integer)
      .case_("@number@", fundamental_e::number)
      .case_("@string@", fundamental_e::string)
      .or_default(std::nullopt);
}

// precondition: e != array && s != object
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
      case fundamental_e::array:
      case fundamental_e::object:
        std::unreachable();
    }
  };
  ref.spec = f2scope();
  return ref;
}

struct enum_schema {
  static constexpr std::string_view discriminator_value = "enum";

  std::optional<schema_ref> type;
  array<Any> values;
};

struct discriminator_desc {
  string propertyName;
  // при конвертации с фронтенда здесь оказывается полный маппинг
  // значение дискриминатора => ссылка на тип который ему соответствует
  string_map<schema_ref> mapping;
};

struct oneof_schema {
  static constexpr std::string_view discriminator_value = "oneOf";

  std::optional<discriminator_desc> discriminator;
  array<schema_ref> schemas;
};

struct anyof_schema {
  static constexpr std::string_view discriminator_value = "anyOf";

  std::optional<discriminator_desc> discriminator;
  array<schema_ref> schemas;
};

struct allof_schema {
  static constexpr std::string_view discriminator_value = "allOf";

  array<schema_ref> schemas;
};

struct object_field_info {
  schema_ref type;
  string name;
  // поле считается required если
  // * указано в required: [...]
  // * тип НЕ nullable
  // * у типа НЕТ default
  // * тип НЕ readOnly/writeOnly (там необходимость передавать зависит от запрос/ответ, но тип то один)
  // if/then/else игнорируются
  bool required = false;
};

struct object_schema {
  static constexpr std::string_view discriminator_value = "object";

  array<object_field_info> fields;
  std::optional<schema_ref> additional_fields;
};

struct array_schema {
  static constexpr std::string_view discriminator_value = "array";

  schema_ref element;
};

struct not_schema {
  static constexpr std::string_view discriminator_value = "not";

  schema_ref type;
};

// соответствует схеме false
// т.е. любой тип должен проваливать проверку
struct never_schema {
  static constexpr std::string_view discriminator_value = "never";
};

// соответствует схеме true
// т.е. любой тип подходит
struct any_schema {
  static constexpr std::string_view discriminator_value = "any";
};

// fundamental | enum | object_type | oneof | anyof | allof
struct schema {
  // generic строка файла
  string spec;
  json_pointer jsonptr;
  // если имени нет это инлайн схема
  std::optional<string> name;
  // представление типа (то что получит пользователь генератора)
  std::variant<any_schema, fundamental_e, enum_schema, oneof_schema, anyof_schema, allof_schema,
               object_schema, array_schema, not_schema, never_schema>
      data;
  // схемы которые не влияют на представление, только на валидацию
  // н-р type: string enum: ... это означает что пользователь увидит строку, но она провалидируется по тому
  // что написано в enum
  std::optional<array<schema_ref>> required;

  std::string scope() const {
    return std::format("{}#{}", spec, jsonptr.escaped());
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

  bool operator==(const schema& x) const noexcept {
    return spec == x.spec && jsonptr == x.jsonptr && name == x.name;
  }
};

[[nodiscard]] inline bool is_meta_key(std::string_view key) noexcept {
  return key.starts_with("x-");
}

// все ключи должны быть is_meta_key(key)
// хранилище разных вспомогательных тегов, которые фронтенд хочет прокинуть бекенду
// н-р deprecated или формат query и тд
using meta_t = string_map<Any>;

struct operation_parameter {
  parameter_location location = parameter_location::path;
  string name;
  schema_ref schema;
  // по умолчанию false
  std::optional<bool> required;
  // аналогично упрощённому encoding из openapi
  // говорит как отправлять этот параметр, н-р application/json
  // по умолчанию такой же как у тела запроса
  std::optional<string> encoding;
  std::optional<meta_t> meta;

  bool operator==(const operation_parameter& r) const noexcept {
    return location == r.location && name == r.name;
  }
};

struct header {
  // не зависит от регистра
  string name;
  // по умолчанию false
  std::optional<bool> required;
  // по умолчанию эквивалентно type: string
  std::optional<schema_ref> schema;

  bool operator==(const header& r) const noexcept {
    return is_equal_ci(name, r.name);
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
      hash.add(std::toupper(c));
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
  // TODO support media type ranges
  // может быть также media type range */* - любой, image/* (image/x* уже невалидно)
  // выигрывает более специализированный
  string contentType;
  schema_ref schema;
  // мапа имя property из schema => как его кодировать конкретно в этом теле
  // application/json и тд контент типы
  std::optional<string_map<string>> membersEncoding;
  // означает, что schema применяется не ко всему телу, а к единице стрима
  // (например openapi 3.2 itemsSchema)
  // по умолчанию false
  std::optional<bool> streaming;

  bool operator==(const body& b) const noexcept {
    return contentType == b.contentType;
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::body> {
  size_t operator()(const ::svarg::ir::body& b) const noexcept {
    return ::svarg::hash_combined(b.contentType);
  }
};

template <>
struct hash<::svarg::ir::schema_ref> {
  size_t operator()(const ::svarg::ir::schema_ref& r) const noexcept {
    return ::svarg::ir::hash_schema(r.spec, r.jsonptr);
  }
};

}  // namespace std

namespace svarg::ir {

enum struct status_range_e {
  _1XX,
  _2XX,
  _3XX,
  _4XX,
  _5XX,
};

inline constexpr std::string_view e2str(status_range_e e) {
  switch (e) {
    case status_range_e::_1XX:
      return "1XX";
    case status_range_e::_2XX:
      return "2XX";
    case status_range_e::_3XX:
      return "3XX";
    case status_range_e::_4XX:
      return "4XX";
    case status_range_e::_5XX:
      return "5XX";
  }
}

inline constexpr void efromstr(std::string_view str, status_range_e& out) {
  using enum status_range_e;
  auto loc = ss::string_switch<status_range_e>(str)
                 .case_("1XX", _1XX)
                 .case_("2XX", _2XX)
                 .case_("3XX", _3XX)
                 .case_("4XX", _4XX)
                 .case_("5XX", _5XX)
                 .or_default(status_range_e(-1));
  if (loc == status_range_e(-1))
    throw parse_error("invalid status range type `{}`", str);
  out = loc;
}

struct response_desc {
  // если респонс дефолтный, то у него выставлен статус 0
  static constexpr int64_t default_status_marker = 0;

  // всегда должен быть одно - либо status либо statusRange
  // допустимые значения - [100; 600)
  std::optional<int64_t> status;
  // допустимые значения 100, 200, 300, 400, 500
  std::optional<status_range_e> statusRange;
  // content-type игнорируется (даже не попадает в этот список)
  // регистронезависимо
  std::optional<std::unordered_set<header>> headers;
  // на каждый contentType своё тело
  std::optional<std::unordered_set<body>> bodies;

  [[nodiscard]] bool is_default() const noexcept {
    return status && *status == default_status_marker;
  }
  void make_default() noexcept {
    if (statusRange)
      statusRange = std::nullopt;
    status = default_status_marker;
  }
  void set_status(std::string_view s) {
    std::optional range = ss::string_switch<std::optional<status_range_e>>(s)
                              .case_("1XX", status_range_e::_1XX)
                              .case_("2XX", status_range_e::_2XX)
                              .case_("3XX", status_range_e::_3XX)
                              .case_("4XX", status_range_e::_4XX)
                              .case_("5XX", status_range_e::_5XX)
                              .or_default(std::nullopt);
    if (range)
      statusRange = *range;
    size_t num = 0;
    if (!try_from_chars(s, num))
      throw parse_error("invalid status, not number and not valid range: `{}`", s);
    if (num == 0)
      throw validate_error("invalid status value 0");
    // не проверяем статус, вдруг кто-то сделал 1023 валидным статусом
    status = num;
  }

  bool operator==(const response_desc& r) const noexcept {
    if (status && r.status)
      return *status == *r.status;
    if (statusRange && r.statusRange)
      return *statusRange == *r.statusRange;
    return false;
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::response_desc> {
  size_t operator()(const ::svarg::ir::response_desc& o) const {
    assert(!(o.status && o.statusRange));
    if (o.status) {
      return ::svarg::hash_combined(*o.status);
    } else if (o.statusRange) {
      // значения из енама меньше 100, поэтому не должны пересекаться с status никогда
      return ::svarg::hash_combined(std::to_underlying(*o.statusRange));
    } else {
      // должен быть status или statusRange
      unreachable();
    }
  }
};

}  // namespace std

namespace svarg::ir {

struct operation {
  // operationId, если нет, используется method + путь (каким-то образом)
  // лучше бы было
  std::optional<string> id;
  // спека где объявлена операция
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
  std::optional<std::unordered_set<body>> bodies;
  std::optional<std::unordered_set<response_desc>> responses;
  std::optional<response_desc> defaultResponse;
  std::optional<meta_t> meta;

  bool operator==(const operation& r) const noexcept {
    // имя не участвует в сравнении, так как при отправлении на тот же путь с тем же методом
    // неважно как оно называется (при этом из разных спек это всё же разные операции)
    return spec == r.spec && method == r.method && is_equal_paths_template_insensitve(path, r.path);
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::schema> {
  size_t operator()(const ::svarg::ir::schema& t) const {
    return ::svarg::ir::hash_schema(t.spec, t.jsonptr);
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

struct ir_object {
  // TODO всё это optional должно быть
  std::unordered_set<schema> schemas;
  std::unordered_set<operation> operations;
  // здесь помимо webhooks лежат callbacks (они то же самое, но ещё вычислимый url)
  std::unordered_set<operation> webhooks;
  // TODO servers

  // объединяет 2 ir в один
  void merge(ir_object obj);

  bool operator==(const ir_object&) const = default;
};

// TODO serialize/parse ir
// TODO операции

}  // namespace svarg::ir

namespace svarg {

template <>
struct discriminator_t<ir::fundamental_e> {
  static constexpr std::string_view value = "fundamental";
};

}  // namespace svarg
