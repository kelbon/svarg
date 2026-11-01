#pragma once

#include "common_types.hpp"

#include "svarg/backend/cxx/support/router.hpp"

#include "api/operations/ping.hpp"
namespace api {

// операции спеки - виртуальные do_X. Маршрут каждой операции в routes() с id < user_route_begin, свои
// маршруты можно добавить туда же с id от user_route_begin, их обрабатывает on_user_route
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

  // маршрут пользователя: id >= user_route_begin
  virtual dd::task<hidi::http_response> on_user_route(size_t, hidi::http_request req,
                                                      hidi::request_context ctx) {
    return on_unknown_operation(std::move(req), std::move(ctx));
  }

  // определение в .cpp спеки, чтобы разбор всех операций не инстанцировался у пользователя
  dd::task<hidi::http_response> handle_request(hidi::http_request req, hidi::request_context ctx) override;

  static constexpr size_t user_route_begin = 1;

  ::svarg::cxx_support::router& routes() noexcept {
    return routes_;
  }

 private:
  static ::svarg::cxx_support::router make_routes();

  ::svarg::cxx_support::router routes_ = make_routes();
};

}  // namespace api
