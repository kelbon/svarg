#pragma once

#include <cassert>

#include "svarg/backend/cxx/support/concepts.hpp"
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

  template <content_type_e C, typename T>
  void arg(std::string_view k, const T& value) {
    svarg::query_encode(k, std::back_inserter(body));
    body.push_back('=');
    body += serialize_into<C>(value);
    body.push_back('&');
  }
};

}  // namespace svarg::cxx_support
