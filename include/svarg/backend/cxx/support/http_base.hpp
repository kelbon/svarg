#pragma once

#include <hidi/http_body.hpp>
#include <hidi/h2client.hpp>
#include <hidi/http_base.hpp>
#include <hidi/h2server.hpp>

namespace svarg::cxx_support {

using hidi::deadline_t;
using hidi::duration_t;
using hidi::endpoint;
using hidi::h2client;
using hidi::h2client_options;
using hidi::h2server;
using hidi::h2server_options;
using hidi::http_body;
using hidi::http_body_bytes;
using hidi::http_header_t;
using hidi::http_headers_t;
using hidi::http_method_e;
using hidi::http_request;
using hidi::http_response;
using hidi::internet_address;
using hidi::request_context;

// для генерации сервера
[[nodiscard]] inline dd::task<http_response> make_bad_request_rsp() {
  co_return http_response{.status = 400};
}
// TODO добавить HTTP коды енам, его e2str/efromstr и в самом http2 вынести это в отдельный неймспейс

}  // namespace svarg::cxx_support
