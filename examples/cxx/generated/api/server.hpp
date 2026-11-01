#pragma once

#include "common_types.hpp"

#include "api/operations/ping.hpp"
namespace api {

struct server : hidi::h2server {
  using hidi::h2server::h2server;

  virtual dd::task<hidi::http_response> do_ping(ping, hidi::http_request, hidi::request_context) = 0;

  // путь и метод не подошли ни к одной операции
  virtual dd::task<hidi::http_response> on_unknown_operation(hidi::http_request, hidi::request_context) {
    co_return hidi::http_response{.status = 404};
  }

  // операция найдена, но запрос не соответствует ей (параметры, тело...)
  virtual dd::task<hidi::http_response> on_bad_request(hidi::http_request, svarg::error_code,
                                                       hidi::request_context) {
    co_return hidi::http_response{.status = 400};
  }

  dd::task<hidi::http_response> handle_request(hidi::http_request req, hidi::request_context ctx) override;
};

}  // namespace api
