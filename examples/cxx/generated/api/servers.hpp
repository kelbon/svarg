#pragma once

#include "common_types.hpp"

#include "api/operations/ping.hpp"
namespace api {
struct server__ : hidi::h2server {
  static std::string make_url() {
    return "/";
  }
  virtual dd::task<hidi::http_response> do_ping(ping, hidi::http_request, hidi::request_context) = 0;

  virtual dd::task<hidi::http_response> on_unknown_operation(hidi::http_request req, hidi::request_context) {
    throw svarg::parse_error("cannot find handler for path `{}`", req.path);
  }
  dd::task<hidi::http_response> handle_request(hidi::http_request req, hidi::request_context ctx) override {
    switch (req.method) {
      case hidi::http_method_e::GET:
        if (svarg::match_path_pattern<"/ping">(svarg::path_and_query(req.path).first)) {
          ping op;
          svarg::error_code ec = svarg::cxx_support::from_request(req, op);
          if (ec)
            return svarg::cxx_support::make_bad_request_rsp();
          return do_ping(std::move(op), std::move(req), std::move(ctx));
        }
      default:
        return on_unknown_operation(std::move(req), std::move(ctx));
        break;
    }
  }
};

}  // namespace api
