#include <moko3/moko3.hpp>

#include <chrono>

// сгенерировано из generated_api/api.yaml (см. test/CMakeLists.txt)
#include "api/server.hpp"
#include "api/client.hpp"

using namespace svarg::cxx_support;

struct test_server : api::server {
  using api::server::server;

  std::string uploaded;
  std::vector<std::string> notes;

  dd::task<hidi::http_response> do_getItem(api::getItem op, hidi::http_request,
                                           hidi::request_context) override {
    api::getItem::response_200 rsp;
    // limit и tag со значениями по умолчанию
    rsp.body = std::format("item{}/{}/{}", op.path.id.get(), op.query.limit.get(), op.query.tag);
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> do_upload(api::upload op, hidi::http_request,
                                          hidi::request_context) override {
    uploaded = op.body.get();
    co_return make_response(api::upload::response_204{});
  }

  dd::task<hidi::http_response> do_putItem(api::putItem op, hidi::http_request,
                                           hidi::request_context) override {
    api::putItem::response_200 rsp;
    rsp.body = op.body;  // эхо, поля по умолчанию уже заполнены
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> do_addNote(api::addNote op, hidi::http_request,
                                           hidi::request_context) override {
    if (auto* text = std::get_if<api::addNote::application_json_body>(&op.body))
      notes.push_back(text->get());
    else if (auto* n = std::get_if<api::addNote::application_problem_json_body>(&op.body))
      notes.push_back(std::to_string(n->get()));
    co_return make_response(api::addNote::response_204{});
  }
};

// поднимает сервер на loopback и выполняет `fn(h2client&)` до завершения
static void run_with_server(test_server& server, auto fn) {
  hidi::internet_address addr =
      server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 0}});
  hidi::h2client client{hidi::endpoint(addr)};

  bool done = false;
  auto run = [&]() -> dd::task<void> {
    co_await fn(client);
    done = true;
  };
  run().start_and_detach();

  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done && std::chrono::steady_clock::now() < until) {
    server.ioctx().poll();
    client.ioctx().poll();
  }
  REQUIRE(done);
}

// отправляет запросы по очереди и возвращает ответы
static std::vector<http_response> exchange(test_server& server, std::vector<http_request> reqs) {
  std::vector<http_response> rsps;
  run_with_server(server, [&](hidi::h2client& client) -> dd::task<void> {
    for (auto& r : reqs)
      rsps.push_back(co_await client.send_request(std::move(r), hidi::deadline_t::never()));
  });
  return rsps;
}

static http_request with_base_path(http_request req, std::string_view base_path) {
  req.path.insert(0, base_path);
  return req;
}

TEST("generated server") {
  api::getItem get;
  get.path.id.get() = 5;
  api::upload up;
  up.body.get() = "data";

  http_request bad_get = with_base_path(make_request(get), "/v1");
  bad_get.path = "/v1/items/0";  // minimum, клиент такое не сериализует
  // GET не должен провалиться в обработку POST
  http_request wrong_method = with_base_path(make_request(up), "/v2");
  wrong_method.method = hidi::http_method_e::GET;
  http_request bad_upload = with_base_path(make_request(up), "/v2");
  bad_upload.body.data.assign(2, '"');  // пустая строка, minLength
  http_request no_query = bad_get;
  no_query.path = "/v1/items/5";
  http_request bad_limit = bad_get;
  bad_limit.path = "/v1/items/5?limit=0";  // minimum у параметра со значением по умолчанию
  http_request put_raw;
  put_raw.method = hidi::http_method_e::PUT;
  put_raw.path = "/v1/items/5/data";
  put_raw.body.content_type = "application/json";
  std::string_view item_json = R"({"name":"n"})";
  put_raw.body.data.assign(item_json.begin(), item_json.end());

  test_server server(hidi::server_ssl_context_ptr(nullptr));
  std::vector rsps = exchange(server, {
                                          with_base_path(make_request(get), "/v1"),
                                          make_request(get),  // без base path
                                          wrong_method,
                                          bad_get,
                                          with_base_path(make_request(up), "/v2"),
                                          bad_upload,
                                          no_query,
                                          bad_limit,
                                          put_raw,
                                      });
  REQUIRE(rsps.size() == 9);

  api::getItem::response_200 item;
  REQUIRE(rsps[0].status == 200);
  REQUIRE(!from_response(rsps[0], item).failed());
  REQUIRE(item.body == "item5/10/all");
  REQUIRE(item.headers.x_version == 1);

  REQUIRE(rsps[1].status == 404);
  REQUIRE(rsps[2].status == 404);
  REQUIRE(rsps[3].status == 400);
  REQUIRE(rsps[4].status == 204);
  REQUIRE(server.uploaded == "data");
  REQUIRE(rsps[5].status == 400);

  // значения по умолчанию: отсутствующие параметры, хедер, поля тела
  REQUIRE(rsps[6].status == 200);
  REQUIRE(!from_response(rsps[6], item).failed());
  REQUIRE(item.body == "item5/10/all");
  std::erase_if(rsps[6].headers, [](auto& h) { return h.name() == "x-version"; });
  item = {};
  item.headers.x_version = 7;
  REQUIRE(!from_response(rsps[6], item).failed());
  REQUIRE(item.headers.x_version == 7);  // отсутствующий хедер не трогает значение

  REQUIRE(rsps[7].status == 400);

  api::putItem::response_200 echoed;
  REQUIRE(rsps[8].status == 200);
  REQUIRE(!from_response(rsps[8], echoed).failed());
  REQUIRE(echoed.body.name == "n" && echoed.body.count == 3);
  REQUIRE(echoed.body.kind.get() == api::components::schemas::Kind::b);
  REQUIRE(echoed.body.tags == std::vector<std::string>{"x"});
}

// middleware: считает запросы и передаёт дальше
struct counting {
  any_client next;
  int* count;

  dd::task<http_response> send_request(http_request req, hidi::deadline_t d) {
    ++*count;
    return next.send_request(std::move(req), d);
  }
};

TEST("generated client") {
  test_server server(hidi::server_ssl_context_ptr(nullptr));
  int count = 0;
  api::getItem::result_t item;
  api::upload::result_t uploaded;

  run_with_server(server, [&](hidi::h2client& h2) -> dd::task<void> {
    api::client cli{counting{any_client(client_ref(h2)), &count}};
    api::client_upload_example_com_v2 upload_cli{client_ref(h2)};

    api::getItem get;
    get.path.id.get() = 7;
    item = co_await cli.send_getItem(get, hidi::deadline_t::never());

    api::upload up;
    up.body.get() = "file";
    uploaded = co_await upload_cli.send_upload(up, hidi::deadline_t::never());

    // несколько тел: выбирается по content type
    api::addNote note;
    note.body.emplace<api::addNote::application_json_body>().get() = "text";
    (void)co_await cli.send_addNote(note, hidi::deadline_t::never());
    note.body.emplace<api::addNote::application_problem_json_body>().get() = 42;
    (void)co_await cli.send_addNote(note, hidi::deadline_t::never());
  });

  REQUIRE(count == 3);
  REQUIRE(server.notes == std::vector<std::string>{"text", "42"});
  REQUIRE(item.get_if_200() && item.get_if_200()->body == "item7/10/all");
  REQUIRE(server.uploaded == "file");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
