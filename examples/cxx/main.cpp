#include <iostream>

#include "generated/api/client.hpp"

#include "generated/api/server.hpp"

struct example_server : api::server {
  using api::server::server;

  dd::task<hidi::http_response> do_ping(api::ping, hidi::http_request, hidi::request_context) override {
    api::ping::response_200 rsp;
    rsp.body._dATA.status = "ok";
    co_return svarg::cxx_support::make_response(rsp);
  }
};

int main() {
  example_server server(hidi::server_ssl_context_ptr(nullptr));  // http, без tls
  hidi::internet_address addr =
      server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 0}});

  hidi::h2client h2{hidi::endpoint(addr)};
  // транспорт можно обернуть в свои middleware (ретраи, хедеры...), см. any_client.hpp
  api::client cli{svarg::cxx_support::client_ref(h2)};

  bool done = false;
  auto ping = [&]() -> dd::task<void> {
    api::ping::result_t rsp = co_await cli.send_ping(api::ping{}, hidi::deadline_t::never());
    std::cout << "ping: " << rsp.body._dATA.status << std::endl;
    done = true;
  };
  ping().start_and_detach();
  // сервер и клиент в одном потоке
  while (!done) {
    server.ioctx().poll();
    h2.ioctx().poll();
  }
  return 0;
}
