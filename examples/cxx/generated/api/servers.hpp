#pragma once

#include "common_types.hpp"

#include "api/operations/ping.hpp"
namespace api {
struct server__ : svarg::http2_server {
  static std::string make_url() {
    return "/";
  }
  virtual dd::task<svarg::http_response> do_ping(ping, svarg::http_request, svarg::request_context) = 0;

  virtual dd::task<svarg::http_response> on_unknown_operation(svarg::http_request req,
                                                              svarg::request_context) {
    throw svarg::parse_error("cannot find handler for path `{}`", req.path);
  }
  dd::task<http_response> handle_request(svarg::http_request req, svarg::request_context ctx) override {
    switch (req.method) {
      case svarg::http_method_e::GET:
        if (svarg::match_path_pattern<"/ping">(svarg::path_and_query(req.path).first)) {
          ping op;
          error_code ec = from_request(req, op);
          if (ec)
            return svarg::make_bad_request_rsp();
          return do_ping(std::move(op), std::move(req), std::move(ctx));
        }
      default:
        return on_unknown_operation(std::move(req), std::move(ctx));
        break;
    }
  }
};

}  // namespace api
