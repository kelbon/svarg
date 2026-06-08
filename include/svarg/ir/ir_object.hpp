#pragma once

#include <boost/json/serialize.hpp>
#include <boost/container_hash/hash.hpp>
#include <unordered_set>

#include <http2/http_base.hpp>
#include "svarg/fundamental_types.hpp"
#include "svarg/openapi_object.hpp"
#include "svarg/utils/parse_ref.hpp"
#include "svarg/utils/path_tools.hpp"
#include "svarg/utils/strcmp.hpp"

namespace svarg::ir {

struct type;

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

// fundamental | name + scope
struct type_ref {
  optional<string> name;
  // REQUIRED если name есть (не включает имя типа)
  optional<string> scope;
  // указано если тип это фундаментальный null/boolean/string/number/integer НЕ array/object
  optional<fundamental_e> fundamental;

  [[nodiscard]] bool refers_to(const type&) const noexcept;
};

struct enum_type {
  array<Any> values;
};

struct oneof_type {
  optional<discriminator_object> discriminator;
  array<type_ref> types;
};

struct anyof_type {
  optional<discriminator_object> discriminator;
  array<type_ref> types;
};

struct allof_type {
  array<type_ref> types;
};

struct object_field_info {
  type_ref type;
  string name;
  // поле считается required если
  // * указано в required: [...]
  // * тип НЕ nullable
  // * у типа НЕТ default
  // * тип НЕ readOnly/writeOnly (там необходимость передавать зависит от запрос/ответ, но тип то один)
  // if/then/else игнорируются
  bool required = false;
};

struct object_type {
  array<object_field_info> fields;
  optional<type_ref> additional_fields;
};

struct array_type {
  type_ref element;
};

struct not_type {
  // TODO inline parse
  type_ref type;
};

// соответствует схеме false
// т.е. любой тип должен проваливать проверку
struct never_type {};

// соответствует схеме true
// т.е. любой тип подходит
struct any_type {};

// fundamental | enum | object_type | oneof | anyof | allof
struct type {
  // spec#jsonptr ref на место где определён тип (не включает имя типа)
  string scope;
  optional<string> name;
  std::variant<fundamental_e, enum_type, oneof_type, anyof_type, allof_type, object_type, array_type,
               not_type, never_type, any_type>
      data;
  // TODO? require? типа требования из anyOf/oneOf/allOf/not когда уже указан type?
  // придётся мб по __inline_schema искать и всё такое
  // TODO? в схему добавить напрямую not, чтобы прямо в схеме накостылять not...?
  // TODO в ir должны быть не типы, а схемы всё таки

  std::string_view spec() const noexcept {
    return parse_ref(scope).file;
  }
  std::string_view jsonptr() const noexcept {
    return parse_ref(scope).json_ptr;
  }

  type_ref ref() const {
    type_ref r;
    if (name)
      r.name = *name;
    r.scope = scope;
    if (std::holds_alternative<fundamental_e>(data))
      r.fundamental = std::get<fundamental_e>(data);
    assert(r.refers_to(*this));
    return r;
  }

  bool operator==(const type& x) const noexcept {
    return scope == x.scope && name == x.name;
  }
};

[[nodiscard]] inline bool is_meta_key(std::string_view key) noexcept {
  return key.starts_with("_meta_");
}

// все ключи должны быть is_meta_key(key)
// хранилище разных вспомогательных тегов, которые фронтенд хочет прокинуть бекенду
// н-р deprecated или формат query и тд
using meta_t = string_map<Any>;

struct operation_parameter {
  parameter_location location = parameter_location::path;
  string name;
  type_ref schema;
  // по умолчанию false
  optional<bool> required;
  // аналогично упрощённому encoding из openapi
  // говорит как отправлять этот параметр, н-р application/json
  // по умолчанию такой же как у тела запроса
  optional<string> encoding;
  optional<meta_t> meta;

  bool operator==(const operation_parameter& r) const noexcept {
    return location == r.location && name == r.name;
  }
};

struct header {
  // не зависит от регистра
  string name;
  // по умолчанию false
  optional<bool> required;
  // по умолчанию эквивалентно type: string
  optional<type_ref> schema;

  bool operator==(const header& r) const noexcept {
    return is_equal_ci(name, r.name);
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::header> {
  size_t operator()(const ::svarg::ir::header& h) const {
    size_t seed = 0;
    // без учёта регистра
    for (char c : h.name)
      boost::hash_combine(seed, std::toupper(c));
    return seed;
  }
};

template <>
struct hash<::svarg::ir::operation_parameter> {
  size_t operator()(const ::svarg::ir::operation_parameter& p) const {
    size_t seed = 0;
    boost::hash_combine(seed, static_cast<std::underlying_type_t<svarg::parameter_location>>(p.location));
    boost::hash_combine(seed, p.name);
    return seed;
  }
};

}  // namespace std

namespace svarg::ir {

struct body {
  // TODO support media type ranges
  // может быть также media type range */* - любой, image/* (image/x* уже невалидно)
  // выигрывает более специализированный
  string contentType;
  type_ref schema;
  // мапа имя property из schema => как его кодировать конкретно в этом теле
  optional<string_map<string>> membersEncoding;

  bool operator==(const body& b) const noexcept {
    return contentType == b.contentType;
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::body> {
  size_t operator()(const ::svarg::ir::body& b) const noexcept {
    size_t seed = 0;
    boost::hash_combine(seed, b.contentType);
    return seed;
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

struct response_desc {
  // всегда должен быть одно - либо status либо statusRange
  // допустимые значения - [100; 600)
  optional<int64_t> status;
  // допустимые значения 100, 200, 300, 400, 500
  optional<status_range_e> statusRange;
  // content-type игнорируется (даже не попадает в этот список)
  // регистронезависимо
  optional<std::unordered_set<header>> headers;
  // на каждый contentType своё тело
  optional<std::unordered_set<body>> bodies;

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
    size_t seed = 0;
    assert(!(o.status && o.statusRange));
    if (o.status) {
      if (*o.status < 100)
        throw ::svarg::parse_error("invalid status");
      boost::hash_combine(seed, *o.status);
    } else if (o.statusRange) {
      // значения из енама меньше 100, поэтому не должны пересекаться с status никогда
      boost::hash_combine(seed,
                          static_cast<std::underlying_type_t<::svarg::ir::status_range_e>>(*o.statusRange));
    } else {
      assert(false);  // должен быть status или statusRange
    }
    return seed;
  }
};

}  // namespace std

namespace svarg::ir {

// TODO $ref резолвить все всегда
struct operation {
  // operationId, если нет, используется method + путь (каким-то образом)
  // лучше бы было
  optional<string> id;
  // спека где объявлена операция
  string spec;
  // путь относительно url сервера. Должен начинаться с / (если не пустой)
  // path + method + spec должны быть уникальны. Шаблонизированность не учитывается
  // если это webhook/callback, то путь может быть runtime expression
  // если opeartion это вебхук, то путь может быть пустым
  string path;
  http2::http_method_e method = http2::http_method_e::GET;
  optional<array<operation_parameter>> parameters;
  // на каждый contentType своё тело
  optional<std::unordered_set<body>> bodies;
  std::unordered_set<response_desc> responses;
  optional<response_desc> defaultResponse;
  optional<meta_t> meta;

  bool operator==(const operation& r) const noexcept {
    // имя не участвует в сравнении, так как при отправлении на тот же путь с тем же методом
    // неважно как оно называется (при этом из разных спек это всё же разные операции)
    return spec == r.spec && method == r.method && is_equal_paths_template_insensitve(path, r.path);
  }
};

}  // namespace svarg::ir

namespace std {

template <>
struct hash<::svarg::ir::type> {
  size_t operator()(const ::svarg::ir::type& t) const {
    size_t seed = 0;
    boost::hash_combine(seed, t.scope);
    if (t.name)
      boost::hash_combine(seed, *t.name);
    return seed;
  }
};

template <>
struct hash<::svarg::ir::operation> {
  size_t operator()(const ::svarg::ir::operation& o) const {
    size_t seed = 0;
    boost::hash_combine(seed, o.spec);
    ::svarg::visit_path_segments(o.path, [&](std::string_view segment) {
      // игнорируем сломанные сегменты типа {{abc} и так далее
      if (segment.starts_with('{')) {
        // игнорируем шаблонизацию, users/{id} == users/{name}
        boost::hash_combine(seed, "{}");
      } else {
        boost::hash_combine(seed, segment);
      }
    });
    boost::hash_combine(seed, o.path);
    boost::hash_combine(seed, static_cast<std::underlying_type_t<http2::http_method_e>>(o.method));
    return seed;
  }
};

}  // namespace std

namespace svarg::ir {

struct ir_object {
  // пока примитивно есть просто типы
  std::unordered_set<type> types;
  std::unordered_set<operation> operations;
  // здесь помимо webhooks лежат callbacks (они то же самое, но ещё вычислимый url)
  std::unordered_set<operation> webhooks;
  // TODO servers

  // объединяет 2 ir в один
  void merge(ir_object obj);
};

// TODO serialize/parse ir
// TODO операции

}  // namespace svarg::ir
