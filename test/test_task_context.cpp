#include <moko3/moko3.hpp>

#include <chrono>

// сгенерировано из generated_api/task_context.yaml с --task-context (см. test/CMakeLists.txt)
#include "task_context/client.hpp"
#include "task_context/server.hpp"

using test_task_ctx::ctx;

struct ping_server : task_context::server {
  using task_context::server::server;

  bool handled_with_ctx = false;

  dd::task<hidi::http_response, ctx> handle(task_context::ping, hidi::http_request,
                                            hidi::request_context) override {
    [[maybe_unused]] ctx& c = co_await dd::this_coro::context;
    handled_with_ctx = true;
    task_context::ping::response_200 rsp;
    rsp.body.n = 42;
    co_return svarg::cxx_support::make_response(rsp);
  }
};

using send_t = decltype(std::declval<task_context::client&>().send(task_context::ping{}, hidi::deadline_t{}));
static_assert(std::is_same_v<send_t, dd::task<task_context::ping::result_t, ctx>>);
using unknown_t = decltype(std::declval<ping_server&>().on_unknown_operation(
    std::declval<hidi::http_request>(), std::declval<hidi::request_context>()));
static_assert(std::is_same_v<unknown_t, dd::task<hidi::http_response, ctx>>);

TEST("generated tasks with context") {
  ping_server server(hidi::server_ssl_context_ptr(nullptr));
  hidi::internet_address addr =
      server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 0}});
  hidi::h2client h2{hidi::endpoint(addr)};

  int created_before = ctx::created;
  bool done = false;
  task_context::ping::result_t result;
  int unknown_status = 0;
  auto run = [&]() -> dd::task<void> {
    task_context::client cli{svarg::cxx_support::client_ref(&h2)};
    result = co_await cli.send(task_context::ping{}, hidi::deadline_t::never());
    hidi::http_response rsp =
        co_await h2.send_request(hidi::http_request{.path = "/unknown"}, hidi::deadline_t::never());
    unknown_status = rsp.status;
    done = true;
  };
  run().start_and_detach();

  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done && std::chrono::steady_clock::now() < until) {
    server.ioctx().poll();
    h2.ioctx().poll();
  }
  REQUIRE(done);
  REQUIRE(server.handled_with_ctx);
  REQUIRE(result.body.n == 42);
  REQUIRE(unknown_status == 404);
  // send клиента, handle и on_unknown_operation сервера
  REQUIRE(ctx::created - created_before >= 3);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
