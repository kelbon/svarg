#pragma once

#include <boost/json/serialize.hpp>

#include "svarg/fundamental_types.hpp"
#include "svarg/openapi_object.hpp"

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

// TODO? пост процессингом все allof перегенерировать в другие типы
struct allof_type {
  array<type_ref> types;
};

struct object_type {
  array<type_ref> fields;
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

// fundamental | enum | object_type | oneof | anyof | allof (TODO? мб генерировать)
struct type {
  // spec#jsonptr ref на место где определён тип (не включает имя типа)
  string scope;
  optional<string> name;
  std::variant<fundamental_e, enum_type, oneof_type, anyof_type, allof_type, object_type, array_type,
               not_type, never_type, any_type>
      data;

  bool operator==(const type& x) const noexcept {
    return scope == x.scope && name == x.name;
  }
};
// TODO parse<type>
// TODO parse<type_ref>
struct ir_object {
  // пока примитивно есть просто типы
  // TODO сделать это мапой
  array<type> types;

  // объединяет 2 ir в один
  void merge(ir_object obj);
};

}  // namespace svarg::ir
