#pragma once

#include <cassert>

#include "svarg/backend/cxx/support/value_format.hpp"
#include "svarg/utils/path_tools.hpp"

namespace svarg::cxx_support {

struct application_x_www_form_urlencoded {
 private:
  std::string body;

 public:
  application_x_www_form_urlencoded() = default;
  explicit application_x_www_form_urlencoded(size_t reserve) {
    body.reserve(reserve);
  }
  std::string take() noexcept {
    if (!body.empty()) {
      assert(body.back() == '&');
      body.pop_back();
    }
    return std::move(body);
  }

  // значение кодируется по формату F (поле с encoding.contentType - в JSON и т.д.)
  template <body_format_e F, typename T>
  void arg(std::string_view k, const T& value) {
    nonbody_arg(k, serialize_value<F>(value));
  }

  // ключ и значение уже закодированы (параметры query со style)
  void encoded_arg(std::string_view k, std::string_view v) {
    body += k;
    body.push_back('=');
    body += v;
    body.push_back('&');
  }

  // значение уже сериализовано (параметры query), только percent-encoding
  void nonbody_arg(std::string_view k, std::string_view value) {
    svarg::query_encode(k, std::back_inserter(body));
    body.push_back('=');
    svarg::query_encode(value, std::back_inserter(body));
    body.push_back('&');
  }
};

}  // namespace svarg::cxx_support
