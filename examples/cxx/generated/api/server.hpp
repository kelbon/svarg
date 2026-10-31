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

  dd::task<hidi::http_response> handle_request(hidi::http_request req, hidi::request_context ctx) override {
    std::string_view path = svarg::path_and_query(req.path).first;
    switch (req.method) {
      case hidi::http_method_e::GET:
        if (svarg::match_path_pattern<"/ping">(path)) {
          ping op;
          svarg::error_code ec = svarg::cxx_support::from_request(req, op, "");
          if (ec)
            return on_bad_request(std::move(req), std::move(ec), std::move(ctx));
          return do_ping(std::move(op), std::move(req), std::move(ctx));
        }
        break;
      default:
        break;
    }
    return on_unknown_operation(std::move(req), std::move(ctx));
  }
};

}  // namespace api
