#pragma once

#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <hidi/http_body_bytes.hpp>

#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/to_tokens.hpp"
#include "svarg/backend/cxx/support/json_stream.hpp"
#include "svarg/backend/cxx/support/parsing_writer.hpp"

// ПИСАТЕЛЬ JSON
//
// События to_tokens пишутся прямо в буфер, без boost::json::value. to_json_raw - запись значения целиком, так
// пишутся тела application/json (json_codec)

namespace svarg::cxx_support {

// рост буфера писателя. Записанное до конца всегда перезаписывается, поэтому заполнять новое не нужно там,
// где это можно не делать
template <typename B>
struct json_buffer_traits {
  static void grow(B& b, size_t size) {
    b.resize(size);
  }
};

template <>
struct json_buffer_traits<hidi::http_body_bytes> {
  static void grow(hidi::http_body_bytes& b, size_t size) {
    b.resize(size, hidi::uninitialized_byte);
  }
};

namespace noexport {

// 0 - символ пишется как есть, 'u' - как \u00XX, иначе как \ и этот символ
inline constexpr std::array<char, 256> json_escapes = [] {
  std::array<char, 256> t{};
  for (int c = 0; c < 0x20; ++c)
    t[c] = 'u';
  t['\b'] = 'b';
  t['\f'] = 'f';
  t['\n'] = 'n';
  t['\r'] = 'r';
  t['\t'] = 't';
  t['"'] = '"';
  t['\\'] = '\\';
  return t;
}();

}  // namespace noexport

// JSON в буфер `b` (std::string, hidi::http_body_bytes...) с позиции `ix`. Буфер растёт с запасом, после
// записи его нужно обрезать до `ix`.
// Запятая пишется после каждого значения, а лишняя перед '}', ']' и в конце (finish) затирается: так не
// нужно помнить, было ли значение до этого
template <typename B>
struct json_writer {
  B& b;
  size_t ix;

  void on_object_begin() {
    put('{');
  }
  void on_object_end() {
    close('}');
  }
  void on_array_begin() {
    put('[');
  }
  void on_array_end() {
    close(']');
  }

  void on_key(std::string_view k) {
    char* p = quoted(k);
    *p++ = ':';
    ix = p - data();
  }
  void on_escaped_key(std::string_view k) {
    char* p = reserve(k.size() + 3);
    p[0] = '"';
    std::memcpy(p + 1, k.data(), k.size());
    p[k.size() + 1] = '"';
    p[k.size() + 2] = ':';
    ix += k.size() + 3;
  }
  void on_string(std::string_view s) {
    char* p = quoted(s);
    *p++ = ',';
    ix = p - data();
  }
  void on_escaped_string(std::string_view s) {
    char* p = reserve(s.size() + 3);
    p[0] = '"';
    std::memcpy(p + 1, s.data(), s.size());
    p[s.size() + 1] = '"';
    p[s.size() + 2] = ',';
    ix += s.size() + 3;
  }

  void on_int64(int64_t v) {
    number(v);
  }
  void on_uint64(uint64_t v) {
    number(v);
  }
  void on_double(double v) {
    // как boost::json: в JSON нет бесконечностей и NaN
    if (std::isnan(v))
      raw("null,");
    else if (std::isinf(v))
      raw(v > 0 ? "1e99999," : "-1e99999,");
    else
      number(v);
  }
  void on_bool(bool v) {
    raw(v ? "true," : "false,");
  }
  void on_null() {
    raw("null,");
  }

  // после последнего события
  void finish() {
    if (ix > 0 && data()[ix - 1] == ',')
      --ix;
  }

 private:
  char* data() {
    return reinterpret_cast<char*>(b.data());
  }
  // место под `n` байт с позиции ix
  char* reserve(size_t n) {
    if (ix + n > b.size()) [[unlikely]]
      json_buffer_traits<B>::grow(b, 2 * (ix + n));
    return data() + ix;
  }
  void raw(std::string_view s) {
    std::memcpy(reserve(s.size()), s.data(), s.size());
    ix += s.size();
  }
  void put(char c) {
    *reserve(1) = c;
    ++ix;
  }
  void close(char c) {
    char* p = reserve(2);
    if (ix > 0 && p[-1] == ',')
      --p;
    p[0] = c;
    p[1] = ',';
    ix = p + 2 - data();
  }
  template <typename T>
  void number(T v) {
    char* p = reserve(32);  // с запасом на любое число
    p = std::to_chars(p, p + 32, v).ptr;
    *p++ = ',';
    ix = p - data();
  }
  // "s" с экранированием, возвращает позицию после закрывающей кавычки
  char* quoted(std::string_view s) {
    char* p = reserve(6 * s.size() + 3);  // худший случай: всё как \u00XX
    *p++ = '"';
    // TODO simd
    for (char c : s) {
      const auto u = static_cast<unsigned char>(c);
      const char x = noexport::json_escapes[u];
      if (!x) {
        *p++ = c;
      } else if (x == 'u') {
        constexpr std::string_view hex = "0123456789abcdef";
        std::memcpy(p, "\\u00", 4);
        p[4] = hex[u >> 4];
        p[5] = hex[u & 0xF];
        p += 6;
      } else {
        p[0] = '\\';
        p[1] = x;
        p += 2;
      }
    }
    *p++ = '"';
    return p;
  }
};

}  // namespace svarg::cxx_support

namespace svarg {

// дописывает JSON `x` в конец `out` (std::string, hidi::http_body_bytes...). При ошибке `out` не меняется
template <typename T, typename B>
void to_json_raw(const T& x, B& out) {
  const size_t start = out.size();
  cxx_support::json_writer<B> w{.b = out, .ix = start};
  try {
    cxx_support::write_tokens(x, w);
#ifndef NDEBUG
    T value;
    cxx_support::parsing_writer<T> validator(value);
    cxx_support::write_tokens(x, validator);
#endif
  } catch (...) {
    out.resize(start);
    throw;
  }
  w.finish();
  out.resize(w.ix);
}

template <typename T>
[[nodiscard]] std::string to_json_string(const T& x) {
  std::string out;
  to_json_raw(x, out);
  return out;
}

}  // namespace svarg
