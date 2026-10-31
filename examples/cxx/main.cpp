#include <iostream>

#include "generated/api/client.hpp"

#include "generated/api/servers.hpp"

struct example_server : api::server__ {
  // TODO rename
  dd::task<hidi::http_response> do_ping(api::ping, hidi::http_request, hidi::request_context) override {
    co_return hidi::http_response{.status = 200};
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
