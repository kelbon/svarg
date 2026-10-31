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

  dd::task<hidi::http_response> do_login(api::login op, hidi::http_request, hidi::request_context) override {
    api::login::response_200 rsp;
    rsp.body = std::format("{}/{}/{}/{}", op.body.user, op.body.remember,
                           op.body.attempts ? std::to_string(*op.body.attempts) : "-",
                           op.body.meta.value_or("-"));
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> do_uploadFiles(api::uploadFiles op, hidi::http_request,
                                               hidi::request_context) override {
    api::uploadFiles::response_200 rsp;
    rsp.body = std::format("{}/{}/{}/{}", op.body.title, op.body.size.value_or(-1),
                           op.body.meta ? op.body.meta->name : "-", op.body.file.value_or("-"));
    co_return make_response(rsp);
  }

  std::string n1;

  // эхо: JSON часть и N2 часть с тем же Content-Id
  dd::task<hidi::http_response> do_transfer(api::transfer op, hidi::http_request,
                                            hidi::request_context) override {
    if (op.body.binaryN1)
      n1 = std::format("{}={}", op.body.binaryN1->headers.content_id.value_or("-"), op.body.binaryN1->value);
    api::transfer::response_200 rsp;
    rsp.body.jsonData = op.body.jsonData;
    if (op.body.binaryN2) {
      rsp.body.binaryN2.emplace();
      rsp.body.binaryN2->headers.content_id = op.body.binaryN2->headers.content_id;
      rsp.body.binaryN2->value = op.body.binaryN2->value;
    }
    co_return make_response(rsp);
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
    else if (auto* n = std::get_if<api::addNote::application_vnd_note_json_body>(&op.body))
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
  bad_upload.body.data.clear();  // пустая строка, minLength
  http_request no_query = bad_get;
  no_query.path = "/v1/items/5";
  http_request bad_limit = bad_get;
  bad_limit.path = "/v1/items/5?limit=0";  // minimum у параметра со значением по умолчанию
  http_request put_raw;
  put_raw.method = hidi::http_method_e::PUT;
  put_raw.path = "/v1/items/5/data";
  put_raw.body.content_type = "application/json";
  http_request form_raw;
  form_raw.method = hidi::http_method_e::POST;
  form_raw.path = "/v1/login";
  form_raw.body.content_type = "application/x-www-form-urlencoded";
  // meta - с encoding application/json
  std::string_view form = "attempts=3&user=a+b%21&meta=%22m%22";
  form_raw.body.data.assign(form.begin(), form.end());
  REQUIRE(make_request([] {
            api::login l;
            l.body.user = "u";
            l.body.meta = "m";
            return l;
          }()).body.strview() == "user=u&remember=false&meta=%22m%22");
  http_request form_bad = form_raw;
  form_bad.body.data.assign(form.begin(), form.begin() + 10);  // нет обязательного user
  // части не по порядку свойств, разбираются по content type; бинарные данные с нулевым байтом
  using namespace std::string_view_literals;
  http_request related;
  related.method = hidi::http_method_e::POST;
  related.path = "/v1/messages";
  related.body.content_type = R"(Multipart/Related; type="application/json"; boundary="b 1")";
  std::string_view related_body =
      "preamble\r\n"
      "--b 1\r\n"
      "Content-Type: application/vnd.3gpp.ngap\r\n"
      "Content-Id: n2\r\n"
      "\r\n"
      "N2\0\x01\r\n"
      "--b 1  \r\n"
      "content-type: application/json\r\n"
      "\r\n"
      R"({"name":"j"})"
      "\r\n--b 1\r\n"
      "Content-Type: application/vnd.3gpp.5gnas\r\n"
      "CONTENT-ID: n1\r\n"
      "\r\n"
      "N1\r\n"
      "--b 1--\r\n"sv;
  related.body.data.assign(related_body.begin(), related_body.end());
  http_request related_bad = related;
  std::string_view bad_body = "--b 1\r\nContent-Type: text/html\r\n\r\nx\r\n--b 1--"sv;
  related_bad.body.data.assign(bad_body.begin(), bad_body.end());
  std::string_view item_json = R"({"name":"n","child":{"name":"c"}})";
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
                                          form_raw,
                                          form_bad,
                                          related,
                                          related_bad,
                                      });
  REQUIRE(rsps.size() == 13);

  api::getItem::response_200 item;
  REQUIRE(rsps[0].status == 200);
  REQUIRE(!from_response(rsps[0], item).failed());
  REQUIRE(item.body == "item5/10/all");
  REQUIRE(item.headers.x_version == 1);

  // параметр с content: application/json кодируется JSON, а не текстом
  get.headers.x_trace = "t";
  REQUIRE(std::ranges::contains(make_request(get).headers, hidi::http_header_t{"x-trace", R"("t")"}));

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
  // инлайн-подсхема свойства: Item_::properties::mode
  REQUIRE(echoed.body.mode.get() == api::components::schemas::Item_::properties::mode::slow);
  // form-urlencoded запрос, text/plain ответ без JSON кавычек
  REQUIRE(rsps[9].status == 200);
  REQUIRE(rsps[9].body_strview() == "a b!/false/3/m");
  REQUIRE(rsps[10].status == 400);

  // multipart/related: части сопоставлены по content type, Content-Id сохранён
  REQUIRE(rsps[11].status == 200);
  REQUIRE(server.n1 == "n1=N1");
  api::transfer::response_200 echo;
  REQUIRE(!from_response(rsps[11], echo).failed());
  REQUIRE(echo.body.jsonData && echo.body.jsonData->name == "j");
  REQUIRE(echo.body.binaryN2 && echo.body.binaryN2->headers.content_id == "n2" &&
          echo.body.binaryN2->value == "N2\0\x01"sv);
  REQUIRE(rsps[12].status == 400);  // часть не подходит ни к одному свойству

  // поле типа самого объекта
  REQUIRE(echoed.body.child && echoed.body.child->name == "c" && !echoed.body.child->child);
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
  api::getItem::result_t item, not_found;
  api::upload::result_t uploaded;
  std::optional<hidi::http_response> unexpected;
  api::login::result_t greeting;
  api::uploadFiles::result_t summary;

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
    note.body.emplace<api::addNote::application_vnd_note_json_body>().get() = 42;
    (void)co_await cli.send_addNote(note, hidi::deadline_t::never());

    api::login login;
    login.body.user = "u&=";
    login.body.remember = true;
    login.body.meta = "x y";
    greeting = co_await cli.send_login(login, hidi::deadline_t::never());

    api::uploadFiles files;
    files.body.title = "t \"q\"";
    files.body.meta.emplace().name = "m";
    files.body.file = std::string("\r\n--\0", 5);
    summary = co_await cli.send_uploadFiles(files, hidi::deadline_t::never());

    // статус не из спецификации (404 на неверный base path): у getItem есть default ответ,
    // у upload нет - исключение
    api::client wrong_path{client_ref(h2), "/nope"};
    not_found = co_await wrong_path.send_getItem(get, hidi::deadline_t::never());
    api::client_upload_example_com_v2 wrong_upload{client_ref(h2), "/nope"};
    try {
      (void)co_await wrong_upload.send_upload(up, hidi::deadline_t::never());
    } catch (unexpected_response& e) {
      unexpected = std::move(e.response);
    }
  });

  REQUIRE(count == 5);
  REQUIRE(greeting.body == "u&=/true/-/x y");
  REQUIRE(summary.body == std::format("t \"q\"/-1/m/{}", std::string("\r\n--\0", 5)));
  REQUIRE(server.notes == std::vector<std::string>{"text", "42"});
  REQUIRE(item.get_if_200() && item.get_if_200()->body == "item7/10/all");
  REQUIRE(server.uploaded == "file");
  REQUIRE(not_found.get_if_default() && not_found.get_if_default()->status == 404);
  REQUIRE(unexpected && unexpected->status == 404);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
