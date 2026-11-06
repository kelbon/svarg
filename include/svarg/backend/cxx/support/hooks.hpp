#pragma once

#include <kelcoro/task.hpp>

#include <optional>

#include "svarg/backend/cxx/support/http_base.hpp"

namespace svarg::cxx_support {

// специализаии, выполняют функцию типизированной middleware,
// в отличие от any_client который не знает про типы
template <typename Op>
struct operation_hooks {
  // вызывается когда запрос уже создан, в конце make_request
  // static void before_send(const Op& op, hidi::http_request& req)

  // вызывается до обработки запроса и может вернуть готовый ответ (он отправится вместо вызова handle)
  // static std::optional<hidi::http_response> before_handle(const Op& op, const hidi::http_request& req);
};

template <typename Op>
void before_send(const Op& op, http_request& req) {
  if constexpr (requires { operation_hooks<Op>::before_send(op, req); })
    operation_hooks<Op>::before_send(op, req);
}

// nullopt - запрос обрабатывается дальше
template <typename Op>
std::optional<http_response> before_handle(const Op& op, const http_request& req) {
  if constexpr (requires { operation_hooks<Op>::before_handle(op, req); })
    return operation_hooks<Op>::before_handle(op, req);
  else
    return std::nullopt;
}

// для сгенерированного сервера: ответ хука как результат обработки
inline dd::task<http_response> ready_response(http_response rsp) {
  co_return rsp;
}

}  // namespace svarg::cxx_support
