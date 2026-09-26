#pragma once

#include "svarg/common_types.hpp"
#include "svarg/utils/indexes.hpp"
#include "svarg/concepts.hpp"

#include <metel/box.hpp>

#include <hpack/integers.hpp>
#include <hpack/strings.hpp>
#include <hpack/basic_types.hpp>

#include <omo/stream_parser.hpp>

/*
Правила обратной совместимости YAP

Обратная совместимость это возможность декодировать новую схему из закодированного по старой схеме

  ОПЕРАЦИЯ             | совместимость
  -----------------------------------------------------------------------------------------------------------
- добавление поля      | если добавленное поле optional - то сработает. Если required - упадёт
- убирание поля        | незнакомое поле при декоде игнорируется (в том числе если индекс слишком большой)
- свап полей местами   | если не использовались индексированные поля, то сработает. Поле определяется по имени
- добавление required  | не сможет декодировать старые данные, если в них этого поля не было
- снятие required      | не влияет на обратную совместимость
- изменение типа поля  | зависит от типа, например изменение с int_t на uint_t сработает если не нужно будет
декодировать отрицательное число, int_t замена на double сработает, uint_t -> int_t сработает
Для структурных типов нужна совместимость полей, интуитивные правила

Изменение имени поля эквивалентно удалению + добавлению
*/

namespace yap {

using hpack::byte_t;
using binary_t = std::vector<byte_t>;
using hpack::In;
using hpack::Out;
using int_t = int64_t;
using uint_t = uint64_t;

using omo::sax_token;
using omo::sax_token_value;
using tokens_consumer = omo::tokens_consumer;

template <typename T>
constexpr inline bool is_optional_field_type = requires(const T& field) {
  field.has_value();
  *field;
};

template <typename T>
constexpr inline bool is_indexed_keys = requires { typename T::yap_indexed_keys_tag; };

template <typename T>
concept yap_object = requires { typename T::yap_object_tag; };

// вместо имён полей записываются индексы, что уменьшает размер,
// но порядок полей может сломать обратную совместимость
#define YAP_INDEXED_KEYS using yap_indexed_keys_tag = int

#define YAP_OBJECT using yap_object_tag = int

// тег для записи строки в более коротком виде ценой скорости енкода/декода
struct huffman_encoded_string {
  std::string str;

  huffman_encoded_string() = default;
  huffman_encoded_string(std::string&& s) noexcept : str(std::move(s)) {
  }
  huffman_encoded_string(std::string_view s) : str(s) {
  }
  huffman_encoded_string(const char* s) : str(s) {
  }

  auto operator<=>(const huffman_encoded_string&) const = default;
};

// поле с этим типом игнорируется в yap::to_binary/from_binary
// Note: если используются индексы вместо имён полей, то такое поле всё равно будет влиять на индекс полей за
// ним
template <typename T>
struct skipped_field {
  T value = {};

  auto operator<=>(const skipped_field&) const = default;
};

template <typename>
struct is_skipped_field : std::false_type {};
template <typename T>
struct is_skipped_field<skipped_field<T>> : std::true_type {};

// первые два бита
enum struct yap01bits_e : byte_t {
  KEY = 0b11'00'0000,        // 2 бита тип, затем также как STRING (имя поля), начало пары key-value
  KEY_INDEX = 0b10'00'0000,  // 2 бита тип, затем индекс поля, начало пары key-value
  STRING = 0b01'00'0000,     // 2 бита тип, затем 6 битный hpack инт размер строки, затем содержимое строки
  LOOK_NEXT2 = 0b00'00'0000  // означает проверить следующие 2 бита
};

// третий и четвёртый бит
enum struct yap23bits_e : byte_t {
  OBJECT_END = 0b00'00'0000,
  // следующий ещё 1 бит (пятый) значит 0 - END_ARRAY, 1 - BEGIN_ARRAY
  ARRAY_BEGIN_OR_END = 0b00'01'0000,
  ARRAY_BEGIN = 0b00'01'1000,
  ARRAY_END = 0b00'01'0000,
  // началом объекта является KEY. Просто один OBJECT_END означает пустой объект
  NONNEGATIVE_INT = 0b00'10'0000,
  NEGATIVE_INT = 0b00'11'0000,

  // double кодируется как строка, bool кодируется как NONNEGATIVE_INT (0 или 1)
};

constexpr yap01bits_e get01bits(byte_t b) noexcept {
  return yap01bits_e(b & 0b1100'0000);
}
constexpr yap23bits_e get23bits(byte_t b) noexcept {
  return yap23bits_e(b & 0b0011'0000);
}

inline tokens_consumer ignore_sax_value(sax_token& tok, dd::with_stack_resource r) {
  switch (tok.kind()) {
    case sax_token::string:
    case sax_token::int64:
    case sax_token::uint64:
    case sax_token::double_:
    case sax_token::bool_:
    case sax_token::null:
      co_return;
    default:
      break;
  }
  size_t depth = 0;
  for (;;) {
    switch (tok.kind()) {
      case sax_token::array_begin:
      case sax_token::object_begin:
        ++depth;
        co_yield {};
        break;
      case sax_token::array_end:
      case sax_token::object_end:
        --depth;
        if (depth == 0)
          co_return;
        [[fallthrough]];
      case sax_token::string:
      case sax_token::int64:
      case sax_token::uint64:
      case sax_token::double_:
      case sax_token::bool_:
      case sax_token::null:
      case sax_token::key:
      case sax_token::part:
        co_yield {};
        break;
    }
  }
}

}  // namespace yap
