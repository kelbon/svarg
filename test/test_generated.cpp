#include <moko3/moko3.hpp>

#include <chrono>

// сгенерировано из generated_api/api.yaml (см. test/CMakeLists.txt)
#include "api/server.hpp"

using namespace svarg::cxx_support;

struct test_server : api::server {
  using api::server::server;

  std::string uploaded;

  dd::task<hidi::http_response> do_getItem(api::getItem op, hidi::http_request,
                                           hidi::request_context) override {
    api::getItem::response_200 rsp;
    rsp.body._dATA = std::format("item{}", op.path.id._dATA);
    co_return make_response(op, rsp);
  }

  dd::task<hidi::http_response> do_upload(api::upload op, hidi::http_request,
                                          hidi::request_context) override {
    uploaded = op.body._dATA._dATA;
    co_return make_response(op, api::upload::response_204{});
  }
};

// отправляет запросы по очереди и возвращает ответы
static std::vector<http_response> exchange(test_server& server, std::vector<http_request> reqs) {
  hidi::internet_address addr =
      server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 0}});
  hidi::h2client client{hidi::endpoint(addr)};

  std::vector<http_response> rsps;
  bool done = false;
  auto run = [&]() -> dd::task<void> {
    for (auto& r : reqs)
      rsps.push_back(co_await client.send_request(std::move(r), hidi::deadline_t::never()));
    done = true;
  };
  run().start_and_detach();

  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done && std::chrono::steady_clock::now() < until) {
    server.ioctx().poll();
    client.ioctx().poll();
  }
  REQUIRE(done);
  return rsps;
}

static http_request with_base_path(http_request req, std::string_view base_path) {
  req.path.insert(0, base_path);
  return req;
}

TEST("generated server") {
  api::getItem get;
  get.path.id._dATA = 5;
  api::upload up;
  up.body._dATA._dATA = "data";

  http_request bad_get = with_base_path(make_request(get), "/v1");
  bad_get.path = "/v1/items/0";  // minimum, клиент такое не сериализует
  // GET не должен провалиться в обработку POST
  http_request wrong_method = with_base_path(make_request(up), "/v2");
  wrong_method.method = hidi::http_method_e::GET;
  http_request bad_upload = with_base_path(make_request(up), "/v2");
  bad_upload.body.data.assign(2, '"');  // пустая строка, minLength

  test_server server(hidi::server_ssl_context_ptr(nullptr));
  std::vector rsps = exchange(server, {
                                          with_base_path(make_request(get), "/v1"),
                                          make_request(get),  // без base path
                                          wrong_method,
                                          bad_get,
                                          with_base_path(make_request(up), "/v2"),
                                          bad_upload,
                                      });
  REQUIRE(rsps.size() == 6);

  api::getItem::response_200 item;
  REQUIRE(rsps[0].status == 200);
  REQUIRE(!from_response(rsps[0], item).failed());
  REQUIRE(item.body._dATA == "item5");

  REQUIRE(rsps[1].status == 404);
  REQUIRE(rsps[2].status == 404);
  REQUIRE(rsps[3].status == 400);
  REQUIRE(rsps[4].status == 204);
  REQUIRE(server.uploaded == "data");
  REQUIRE(rsps[5].status == 400);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
