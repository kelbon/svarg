#pragma once

// пользовательские кодеки для test_generated, подключаются в каждый сгенерированный файл через
// svarg --file-prefix (см. test/CMakeLists.txt)

#include <algorithm>
#include <cctype>

#include "svarg/backend/cxx/support/codec.hpp"
#include "svarg/backend/cxx/support/value_format.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"

namespace test_codecs {

using namespace svarg::cxx_support;

// "a=x;n=1" для объектов с простыми свойствами
struct kv_codec {
  template <typename T>
  static void serialize(const T& obj, std::string_view, svarg::content_type_params&, http_body_bytes& out) {
    std::string s;
    FOR_EACH_FIELD_INDEX_OF(T) {
      if (is_field_present<I>(obj))
        s += std::format("{}{}={}", s.empty() ? "" : ";", get_field_name<I, T>(),
                         serialize_nonbody_into(get_field<I>(obj)));
    };
    append(out, s);
  }
  template <typename T>
  static svarg::error_code parse(std::string_view, std::string_view data, T& out) {
    while (!data.empty()) {
      std::string_view pair = data.substr(0, data.find(';'));
      data.remove_prefix(std::min(data.size(), pair.size() + 1));
      size_t eq = pair.find('=');
      std::optional i = get_field_index<T>(pair.substr(0, eq));
      if (!i || eq == pair.npos)
        return svarg::parse_error("bad kv pair `{}`", pair);
      svarg::error_code ec = visit_field_index<fields_count<T>()>(
          [&]<size_t I>() { return parse_nonbody_from(pair.substr(eq + 1), get_field<I>(out)); }, *i);
      if (ec)
        return ec;
    }
    return {};
  }
};

// строка в верхнем регистре на проводе
struct upper_codec {
  static void serialize(const std::string& v, std::string_view, svarg::content_type_params&,
                        http_body_bytes& out) {
    std::string s = v;
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::toupper(c); });
    append(out, s);
  }
  static svarg::error_code parse(std::string_view, std::string_view data, std::string& out) {
    out = data;
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
    return {};
  }
};

}  // namespace test_codecs

// по суффиксу: любой медиа тип ...+kv
template <>
struct svarg::cxx_support::media_codec<"+kv"> : test_codecs::kv_codec {};

// по точному медиа типу
template <>
struct svarg::cxx_support::media_codec<"application/x-upper"> : test_codecs::upper_codec {};
