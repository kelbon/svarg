#include <iostream>

#include "generated/api/client.hpp"

#include "generated/api/server.hpp"

struct example_server : api::server {
  using api::server::server;

  dd::task<hidi::http_response> do_ping(api::ping op, hidi::http_request, hidi::request_context) override {
    api::ping::response_200 rsp;
    rsp.body._dATA.status = "ok";
    // ответ проверяется на принадлежность операции при компиляции
    co_return svarg::cxx_support::make_response(op, rsp);
  }
};

int main() {
  // TODO
  example_server root;
  svarg::cxx_support::h2clients_pool pool;
  // TODO может мне достаточно сделать просто клиента, а уже вокруг накрутят свои пулы если надо и тд?
  //  я клиента даю, возможность ему адрес сказать и тд. Возможность задать адрес сервера в апи в том числе
  //  для какой-то функции и мб проверка что адрес совпадает
  // TODO изучить вопрос что в других фреймворках
  api::client cli(pool);
  std::cout << "svarg_simple_cxx_example: generated header included OK\n";
  return 0;
}
