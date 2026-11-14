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

  // вызывается до обработки запроса, может вернуть пустую task (тогда будет обычная обработка запроса)
  // если task не пуста, то она вызывается ВМЕСТО обработки запроса
  // обработка по умолчанию эквивалента return handle(std::move(op), std::move(request), std::move(ctx))
  // NOTE: аргументы по ссылкам валидны лишь до конца синхронного вызова. Если планируется хотя бы один
  // co_await - сделайте move template<typename Server> static dd::task<hidi::http_response>
  // do_handle_request(Op&, hidi::http_request&, Server& server,
  //                                                     hidi::request_context& ctx);
};

template <typename Op>
void before_send(const Op& op, http_request& req) {
  if constexpr (requires { operation_hooks<Op>::before_send(op, req); })
    operation_hooks<Op>::before_send(op, req);
}

template <typename Op, typename Server>
dd::task<hidi::http_response> do_handle_request(Op& op, http_request& req, Server& server,
                                                hidi::request_context& ctx) {
  if constexpr (requires { operation_hooks<Op>::do_handle_request(op, req, server, ctx); })
    return operation_hooks<Op>::do_handle_request(op, req, server, ctx);
  else
    return dd::task<hidi::http_response>{};
}

inline dd::task<http_response> ready_response(http_response rsp) {
  co_return rsp;
}

// задача обработчика с контекстом (svarg --task-context) -> dd::task, которую ждёт hidi
template <typename Ctx>
dd::task<http_response> to_plain_task(dd::task<http_response, Ctx> t) {
  co_return co_await std::move(t);
}

}  // namespace svarg::cxx_support
