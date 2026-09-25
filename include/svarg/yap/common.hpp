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

  TODO тест на слишком большой индекс
*/

namespace yap {

using hpack::byte_t;
using binary_t = std::vector<byte_t>;
using hpack::In;
using hpack::Out;
using int_t = int64_t;
using uint_t = uint64_t;
using svarg::error_code;

using omo::sax_token;
using omo::sax_token_value;
using tokens_consumer = omo::tokens_consumer;

template <typename T>
constexpr inline bool is_optional_field_type = requires(const T& field) { field.has_value(); };

// TODO indexed fields

// Note: добавлять больше нельзя, должно вмещаться в 3 бита (8 штук)
enum struct type_e : byte_t {
  END_OBJECT = 0b0000'0000,  // в конце объекта (после всех KEY+VALUE) идёт это. Если просто стоит END_OBJECT
                             // без предварительного KEY это означает пустой объект
  BOOL = 0b0010'0000,        // одним байтом, 3 бита тип и дальше нижний бит 0 или 1, остальные 0
  NONNEGATIVE_INT = 0b0100'0000,  // 3 бита тип, далее значение хпак кодированием (5 битным, чтобы первые 3
                                  // бита не были затронуты)
  NEGATIVE_INT = 0b0110'0000,     // также как NONNEGATIVE_INT, но кодируется -х
  FLOAT = 0b1000'0000,   // 3 бита тип, длина в том же байте, дальше строчным представлением to/from chars
  STRING = 0b1010'0000,  // 3 бита тип, дальше длина в том же байте хпаком, потом значение
  ARRAY = 0b1100'0000,   // 3 бита тип, дальше количество элементов в том же байте хпаком, потом элементы
  KEY = 0b1110'0000,  // 3 бита тип, KEY означаает начало ключ-значения. Сначала идёт ключ (также как строка)
  // Набор последовательных KEY-VALUE по сути и является объектом. KEY невалиден без последующего значения

  // НИЧЕГО НЕ ДОБАВЛЯТЬ (все 8 штук под 3 бита использованы)
};

constexpr type_e get_type(byte_t b) noexcept {
  return type_e(b & 0b1110'0000);
}

// сколько бит в первом байте hpack кодированного инта доступно для чтения
constexpr inline int INT_WIDTH = 5;

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
