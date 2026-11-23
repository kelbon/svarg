#pragma once

#include <string_view>

#include <omo/token_feeder.hpp>

#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/from_tokens.hpp"

namespace svarg::cxx_support {

// писатель для to_tokens, который разбирает события в `out` через from_tokens - тем же разбором, что у
// входящего JSON, с проверками требований и ограничений схемы. Ошибка разбора - исключение из события
template <typename T>
struct parsing_writer {
 private:
  omo::token_feeder feeder;

 public:
  explicit parsing_writer(T& out)
      : feeder(
            [&out](sax_token& tok, dd::with_stack_resource r) -> tokens_consumer {
              return from_tokens_subobject(out, tok, r);
            },
            {}) {
  }

  void begin_object() {
    feeder.on_object_begin();
  }
  void end_object() {
    feeder.on_object_end();
  }
  void begin_array() {
    feeder.on_array_begin();
  }
  void end_array() {
    feeder.on_array_end();
  }
  void key(std::string_view k) {
    feeder.on_key(k);
  }
  // экранировать не нужно, значит текст тот же
  void escaped_key(std::string_view k) {
    feeder.on_key(k);
  }
  void string(std::string_view s) {
    feeder.on_string(s);
  }
  void escaped_string(std::string_view s) {
    feeder.on_string(s);
  }
  void int64(int64_t v) {
    feeder.on_int64(v);
  }
  void uint64(uint64_t v) {
    feeder.on_uint64(v);
  }
  void double_(double v) {
    feeder.on_double(v);
  }
  void bool_(bool v) {
    feeder.on_bool(v);
  }
  void null() {
    feeder.on_null();
  }

  // после последнего события
  void finish() {
    if (!feeder.is_done())
      throw parse_error("value ended, but its parser did not");
  }
};

}  // namespace svarg::cxx_support
