#include <moko3/moko3.hpp>

#include <chrono>
#include <set>
#include <print>

#include <boost/json.hpp>

// сгенерировано из generated_api/api.yaml (см. test/CMakeLists.txt)
#include "api/server.hpp"
#include "api/client.hpp"
#include "api/all_webhooks.hpp"
#include "api/all_schemas.hpp"

using namespace svarg::cxx_support;

struct test_server : api::server {
  using api::server::server;

  std::string uploaded;
  std::string request_id;
  std::vector<std::string> notes;

  dd::task<hidi::http_response> handle(api::getItem op, hidi::http_request, hidi::request_context) override {
    if (op.headers.x_request_id)
      request_id = *op.headers.x_request_id;
    if (op.path.id.get() == 13)  // тело ответа - не JSON
      co_return hidi::http_response{
          .status = 200, .headers = {{"content-type", "application/json"}}, .body = {'"', 'a'}};
    api::getItem::response_200 rsp;
    // limit и tag со значениями по умолчанию
    rsp.body = std::format("item{}/{}/{}", op.path.id.get(), op.query.limit.get(), op.query.tag);
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(api::upload op, hidi::http_request, hidi::request_context) override {
    uploaded = op.body.get();
    co_return make_response(api::upload::response_204{});
  }

  dd::task<hidi::http_response> handle(api::login op, hidi::http_request, hidi::request_context) override {
    api::login::response_200 rsp;
    rsp.body =
        std::format("{}/{}/{}/{}", op.body.user, op.body.remember,
                    op.body.attempts ? std::to_string(*op.body.attempts) : "-", op.body.meta.value_or("-"));
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(api::uploadFiles op, hidi::http_request,
                                       hidi::request_context) override {
    api::uploadFiles::response_200 rsp;
    rsp.body = std::format("{}/{}/{}/{}", op.body.title, op.body.size.value_or(-1),
                           op.body.meta ? op.body.meta->name : "-", op.body.file.value_or("-"));
    co_return make_response(rsp);
  }

  std::string n1;

  // эхо: JSON часть и N2 часть с тем же Content-Id
  dd::task<hidi::http_response> handle(api::transfer op, hidi::http_request, hidi::request_context) override {
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

  dd::task<hidi::http_response> handle(api::getFile op, hidi::http_request, hidi::request_context) override {
    api::getFile::response_200 rsp;
    rsp.body = std::format("{}|{}", op.path.name, op.path.ext);
    co_return make_response(rsp);
  }

  static std::string join(const auto& items) {
    std::string r;
    if constexpr (requires { items.has_value(); }) {
      if (items)
        return join(*items);
    } else {
      for (auto& x : items)
        r += std::format("{}{}", r.empty() ? "" : " ", x);
    }
    return r;
  }

  dd::task<hidi::http_response> handle(api::search op, hidi::http_request, hidi::request_context) override {
    auto& q = op.query;
    api::search::response_200 rsp;
    rsp.body = std::format("ids={} tags={} filter={}/{} point={}/{} list={} prefs={}", join(q.ids),
                           join(q.tags), q.filter && q.filter->color ? *q.filter->color : "-",
                           q.filter && q.filter->min ? *q.filter->min : -1, q.point ? q.point->x : -1,
                           q.point && q.point->y ? *q.point->y : -1, join(op.headers.x_list),
                           join(op.cookies.prefs));
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(api::getCell op, hidi::http_request, hidi::request_context) override {
    api::getCell::response_200 rsp;
    rsp.body = std::format("{}:{} {}", op.path.cell.row, op.path.cell.col, join(op.path.tags.get()));
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(api::postKv op, hidi::http_request, hidi::request_context) override {
    api::postKv::response_200 rsp;
    rsp.body = std::format("{}-{}", op.body.a, op.body.n.value_or(0));
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(api::putItem op, hidi::http_request, hidi::request_context) override {
    api::putItem::response_200 rsp;
    rsp.body = op.body;  // эхо, поля по умолчанию уже заполнены
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(api::addNote op, hidi::http_request, hidi::request_context) override {
    if (auto* text = std::get_if<api::addNote::application_json_body>(&op.body))
      notes.push_back(text->get());
    else if (auto* n = std::get_if<api::addNote::application_vnd_note_json_body>(&op.body))
      notes.push_back(std::to_string(n->get()));
    co_return make_response(api::addNote::response_204{});
  }

  // бинарное тело ответа: байты как есть, в том числе '\0'
  // ответ потоком: 1000 частей по name + "\0\xff"
  dd::task<hidi::http_response> handle(api::getBlob op, hidi::http_request,
                                       hidi::request_context ctx) override {
    if (op.path.name == "missing")
      co_return make_response(api::getBlob::response_404{.body = "no " + op.path.name});
    api::getBlob::response_200 rsp;
    co_return ctx.stream_response(rsp.status, make_response_headers(rsp),
                                  repeat_parts(op.path.name + std::string("\0\xff", 2), 1000));
  }

  dd::task<hidi::http_response> handle(api::getEvents, hidi::http_request,
                                       hidi::request_context ctx) override {
    api::getEvents::response_200 rsp;
    rsp.headers.x_count = 3;
    co_return ctx.stream_response(rsp.status, make_response_headers(rsp), repeat_parts("data: x\n\n", 3));
  }

  // у одного статуса JSON и поток: медиа тип потока выбирается телом
  dd::task<hidi::http_response> handle(api::getChat op, hidi::http_request,
                                       hidi::request_context ctx) override {
    api::getChat::response_200 rsp;
    if (!op.query.stream || !*op.query.stream) {
      rsp.body.emplace<api::getChat::response_200::application_json_body>().get() = "whole";
      co_return make_response(rsp);
    }
    rsp.body.emplace<api::getChat::response_200::text_event_stream_body>();
    co_return ctx.stream_response(rsp.status, make_response_headers(rsp), repeat_parts("data: y\n\n", 2));
  }

  // запрос без авторизации разбирается, проверка здесь
  // "Bearer bad" -> перехват do_handle_request с ответом 403
  // "Bearer good" -> проверка здесь и 200, остальное 401
  dd::task<hidi::http_response> handle(api::getSecure op, hidi::http_request,
                                       hidi::request_context) override {
    if (op.headers.authorization == "Bearer good" || op.query.key == "k")
      co_return make_response(api::getSecure::response_200{});
    co_return make_response(api::getSecure::response_401{});
  }

  dd::task<hidi::http_response> handle(api::getStyles, hidi::http_request, hidi::request_context) override {
    co_return make_response(api::getStyles::response_204{});
  }

  // день из параметра пути (формат date) обратно в JSON
  dd::task<hidi::http_response> handle(api::getDay op, hidi::http_request, hidi::request_context) override {
    api::getDay::response_200 rsp;
    rsp.body.day = op.path.day;
    rsp.body.count.get() = 1;
    co_return make_response(rsp);
  }

  static hidi::streaming_body_t repeat_parts(std::string part, int n) {
    for (int i = 0; i < n; ++i)
      co_yield std::span<const hidi::byte_t>((const hidi::byte_t*)part.data(), part.size());
  }

  dd::task<hidi::http_response> handle(api::putBlob op, hidi::http_request, hidi::request_context) override {
    api::putBlob::response_200 rsp;
    rsp.body = op.body.size();
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> on_user_route(size_t id, hidi::http_request req,
                                              hidi::request_context) override {
    hidi::http_response rsp{.status = 200};
    std::string body = std::format("user{} {}", id - user_route_begin, req.path);
    rsp.body.assign(body.begin(), body.end());
    co_return rsp;
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

TEST("enum constants colliding with members") {
  using api::components::schemas::Reaction;
  // совпавшая константа получает суффикс '_'
  static_assert(Reaction::values[0] == "+1" && Reaction::values[1] == "-1");
  Reaction r;
  r.get() = Reaction::_1_;
  REQUIRE(svarg::to_json(r) == "-1");
  r.get() = Reaction::get_;
  REQUIRE(svarg::to_json(r) == "get");
  r.get() = Reaction::values_;
  REQUIRE(svarg::to_json(r) == "values");
}

TEST("property named like a service member") {
  api::components::schemas::Item item;
  item.name = "n";
  item.fields = "f";
  REQUIRE(svarg::to_json(item).as_object().at("fields") == "f");
}

TEST("json media type by specialization") {
  api::postKv::response_201 rsp;
  rsp.body.a = "x";
  hidi::http_response http = make_response(rsp);
  REQUIRE(http.body_strview() == R"({"a":"x"})");
  api::postKv::response_201 back;
  REQUIRE(!from_response(http, back).failed() && back.body.a == "x");
}

TEST("formats") {
  using api::components::schemas::Formats;
  using test_codecs::date;
  const boost::json::value json = boost::json::parse(R"({"day": "2026-11-04", "count": 5, "ratio": 0.5,
      "days": ["2000-01-02"], "email": "a@b", "since": "1999-12-31"})");
  Formats f;
  REQUIRE(!svarg::try_from_json(json, f).failed());
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(f.count.get())>, std::int32_t>);
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(*f.ratio)>, float>);
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(*f.email)>, std::string>);
  REQUIRE(f.day.get() == date{2026, 11, 4});
  REQUIRE(f.count.get() == 5 && *f.ratio == 0.5f && *f.email == "a@b");
  // minLength элементов days не проверяется
  REQUIRE(f.days->size() == 1 && (*f.days)[0] == date{2000, 1, 2});
  REQUIRE(*f.since == date{1999, 12, 31});
  REQUIRE(svarg::to_json(f) == json);
  // ограничение встроенного формата проверяется, значение вне int32 - ошибка
  REQUIRE(svarg::try_from_json(boost::json::parse(R"({"day": "2026-11-04", "count": -1})"), f).failed());
  REQUIRE(
      svarg::try_from_json(boost::json::parse(R"({"day": "2026-11-04", "count": 3000000000})"), f).failed());
  REQUIRE(svarg::try_from_json(boost::json::parse(R"({"day": "04.11.2026", "count": 1})"), f).failed());

  // параметр пути - текст типа формата
  api::getDay op;
  op.path.day.get() = date{2026, 1, 2};
  http_request req = make_request(op);
  REQUIRE(req.path == "/days/2026-01-02");
  test_server server(hidi::server_ssl_context_ptr(nullptr));
  std::vector rsps = exchange(server, {with_base_path(std::move(req), "/v1")});
  api::getDay::response_200 rsp;
  REQUIRE(!from_response(rsps[0], rsp).failed());
  REQUIRE(rsp.body.day.get() == date{2026, 1, 2} && rsp.body.count.get() == 1);
}

TEST("optional authorization") {
  // незаполненная авторизация не отправляется
  api::getSecure op;
  REQUIRE(make_request(op).headers.empty());
  op.query.key = "k";
  REQUIRE(make_request(op).path == "/secure?key=k");
  api::getSecure bearer;
  bearer.headers.authorization = "Bearer good";

  test_server server(hidi::server_ssl_context_ptr(nullptr));
  api::getSecure::result_t anonymous, by_key, by_bearer;
  run_with_server(server, [&](hidi::h2client& h2) -> dd::task<void> {
    api::client cli{client_ref(&h2)};
    anonymous = co_await cli.send(api::getSecure{}, hidi::deadline_t::never());
    by_key = co_await cli.send(op, hidi::deadline_t::never());
    by_bearer = co_await cli.send(bearer, hidi::deadline_t::never());
  });
  REQUIRE(anonymous.get_if_401() && by_key.get_if_200() && by_bearer.get_if_200());
}

// хуки - в test_codecs.hpp
TEST("operation hooks") {
  test_server server(hidi::server_ssl_context_ptr(nullptr));
  api::getSecure::result_t by_hook, by_async_hook;
  bool rejected = false;
  run_with_server(server, [&](hidi::h2client& h2) -> dd::task<void> {
    api::client cli{client_ref(&h2)};
    test_codecs::current_token = "good";
    by_hook = co_await cli.send(api::getSecure{}, hidi::deadline_t::never());
    // do_handle_request асинхронно меняет токен и вызывает обработчик
    test_codecs::current_token = "exchange";
    by_async_hook = co_await cli.send(api::getSecure{}, hidi::deadline_t::never());
    // do_handle_request отвечает 403 вместо обработчика
    test_codecs::current_token = "bad";
    try {
      (void)co_await cli.send(api::getSecure{}, hidi::deadline_t::never());
    } catch (unexpected_response& e) {
      rejected = e.response.status == 403;
    }
  });
  test_codecs::current_token.reset();
  REQUIRE(by_hook.get_if_200() && by_async_hook.get_if_200() && rejected);
}

TEST("webhook") {
  api::itemChanged hook;
  hook.body.id = 5;
  http_request req = make_request(hook);
  REQUIRE(req.method == hidi::http_method_e::POST && req.body.content_type == "application/json");
  // получатель вебхука разбирает запрос так же, как сервер операции
  api::itemChanged parsed;
  REQUIRE(!from_request(req, parsed) && parsed.body.id == 5);
}

// JSON. Способы разбора - DOM, поток целиком, поток по частям - дают один и тот же итог: значение или вид
// ошибки. DOM - эталон. Разобранное значение проходит круг сериализация -> разбор без изменений

// итог разбора, по которому сравниваются способы: "ok: <значение, сериализованное снова>" или вид ошибки.
// Сообщения ошибок не сравниваются
template <typename T>
static std::string json_outcome(svarg::error_code& ec, const T& v) {
  try {
    ec.rethrow_if_failed();
  } catch (svarg::validate_error&) {
    return "validate error";
  } catch (svarg::parse_error&) {
    return "parse error";
  }
  if constexpr (!requires { svarg::to_json_value<T>{}(v); }) {
    return "ok";  // not - только требование, не сериализуется
  } else {
    try {
      svarg::content_type_params params;
      http_body_bytes out;
      json_codec::serialize(v, "application/json", params, out);
      return "ok: " + std::string((const char*)out.data(), out.size());
    } catch (std::exception& e) {
      // разобранное значение не сериализуется: null в необязательном поле без null в схеме DOM принимает как
      // отсутствие поля, а требование проверялось по исходному JSON
      return std::format("ok, not serializable: {}", e.what());
    }
  }
}

template <typename T>
static std::string decode_json_dom(std::string_view json) {
  T v;
  svarg::error_code ec = json_codec::parse("application/json", json, v);
  return json_outcome(ec, v);
}

// данные режутся на куски по позициям `cuts` (по возрастанию)
template <typename T>
static std::string decode_json_stream(std::string_view json, std::span<const size_t> cuts) {
  T v;
  svarg::error_code ec;
  json_codec::stream_parser<T> parser("application/json", v);
  try {
    size_t from = 0;
    for (size_t cut : cuts) {
      parser.feed(json.substr(from, cut - from), /*last=*/false);
      from = cut;
    }
    parser.feed(json.substr(from), /*last=*/true);
  } catch (svarg::parse_error& e) {
    ec = std::move(e);
  } catch (svarg::validate_error& e) {
    ec = std::move(e);
  }
  return json_outcome(ec, v);
}

enum struct json_check_e {
  quick,       // поток целиком, по байту и по половинам
  exhaustive,  // и разрез на каждой позиции, и каждый обрывок данных
};

template <typename T>
static void check_json(std::string_view json, json_check_e depth = json_check_e::quick) {
  const std::string expected = decode_json_dom<T>(json);
  auto require_as_dom = [&](std::string_view input, const std::string& got, std::string_view how) {
    if (got != expected) {
      std::println("json: {}\ninput: {}\n{}: `{}`\nDOM:  `{}`", json, input, how, got, expected);
      REQUIRE(false);
    }
  };
  auto all_ways = [&](std::string_view input, bool every_cut) {
    std::vector<size_t> bytes;
    for (size_t i = 1; i < input.size(); ++i)
      bytes.push_back(i);
    require_as_dom(input, decode_json_stream<T>(input, {}), "stream");
    require_as_dom(input, decode_json_stream<T>(input, bytes), "stream by bytes");
    size_t half[] = {input.size() / 2};
    require_as_dom(input, decode_json_stream<T>(input, half), "stream by halves");
    if (every_cut) {
      for (size_t& cut : bytes)
        require_as_dom(input, decode_json_stream<T>(input, std::span(&cut, 1)),
                       std::format("stream cut at {}", cut));
    }
  };
  all_ways(json, depth == json_check_e::exhaustive);

  // круг: сериализация разбирается в то же значение (и сериализуется так же) всеми способами
  if (expected.starts_with("ok: ")) {
    std::string serialized = expected.substr(4);
    require_as_dom(serialized, decode_json_dom<T>(serialized), "DOM of serialized");
    all_ways(serialized, false);
  }

  if (depth == json_check_e::exhaustive) {
    // каждый обрывок - ошибка, одна и та же у всех способов (обрыв посреди строки, числа, вложенных значений)
    for (size_t size = 0; size < json.size(); ++size) {
      std::string_view prefix = json.substr(0, size);
      if (prefix.find_first_not_of(" \n") == std::string_view::npos ||
          json.substr(size).find_first_not_of(" \n") == std::string_view::npos)
        continue;  // пусто или обрезаны только пробелы в конце
      check_json<T>(prefix);
      REQUIRE(decode_json_dom<T>(prefix).ends_with("error"));
    }
  }
}

// все мутации документа: каждое значение заменяется значениями других видов, у каждого объекта удаляется
// каждый ключ и добавляется каждый из лишних (неизвестный и упомянутые в требованиях схем)
static std::vector<std::string> json_mutations(const boost::json::value& doc) {
  static const boost::json::value replacements[] = {nullptr,
                                                    true,
                                                    0,
                                                    -1,
                                                    1.5,
                                                    "",
                                                    "x",
                                                    "red",
                                                    "2026-01-02",
                                                    boost::json::array{},
                                                    boost::json::object{},
                                                    boost::json::array{1, "a"},
                                                    boost::json::object{{"petType", "Dog"}},
                                                    boost::json::object{{"r", 1}},
                                                    boost::json::object{{"k", "v"}, {"z", 1}},
                                                    boost::json::array{boost::json::object{{"b", 1}}}};
  std::vector<std::string> pointers;
  auto collect = [&](this auto& self, const boost::json::value& v, std::string ptr) -> void {
    pointers.push_back(ptr);
    if (auto* o = v.if_object())
      for (auto& [k, x] : *o)
        self(x, ptr + "/" + std::string(k));
    else if (auto* a = v.if_array())
      for (size_t i = 0; i < a->size(); ++i)
        self((*a)[i], ptr + "/" + std::to_string(i));
  };
  collect(doc, "");
  std::vector<std::string> result;
  auto at = [](boost::json::value& d, const std::string& ptr) -> boost::json::value& {
    return ptr.empty() ? d : d.at_pointer(ptr);
  };
  for (const std::string& ptr : pointers) {
    for (const boost::json::value& r : replacements) {
      boost::json::value d = doc;
      at(d, ptr) = r;
      result.push_back(boost::json::serialize(d));
    }
    if (const boost::json::object* o = doc.at_pointer(ptr).if_object()) {
      for (auto& [k, _] : *o) {
        boost::json::value d = doc;
        at(d, ptr).as_object().erase(k);
        result.push_back(boost::json::serialize(d));
      }
      for (std::string_view key : {"unknown", "other", "x", "z", "forbidden"}) {
        if (o->contains(key))
          continue;
        for (const boost::json::value& r :
             {boost::json::value("y"), boost::json::value(boost::json::array{1, {{"a", 2}}})}) {
          boost::json::value d = doc;
          at(d, ptr).as_object()[key] = r;
          result.push_back(boost::json::serialize(d));
        }
      }
    }
  }
  return result;
}

// документ проверяется полностью, его мутации - быстро
template <typename T>
static void check_json_with_mutations(std::string_view json) {
  check_json<T>(json, json_check_e::exhaustive);
  for (const std::string& m : json_mutations(boost::json::parse(json)))
    check_json<T>(m);
}

TEST("json") {
  using namespace api::components::schemas;
  // почти все виды схем: объекты, enum, рекурсия, oneOf и anyOf с дискриминатором, требования, ограничения,
  // additionalProperties, nullable
  check_json_with_mutations<Item>(R"({
    "name": "a\"b", "count": 12345, "kind": "a", "mode": "fast", "tags": ["x", "yy"],
    "child": {"name": "c", "tags": []}, "color": "red", "filter": {"kind": "k", "id": "i"},
    "pet": {"petType": "kitty", "lives": 9}, "anyPet": {"petType": "Dog", "bark": true}, "reaction": "+1",
    "fields": "f", "tree": {"children": [{"children": []}]}, "nulls": {"a": null, "b": "b"},
    "strict": {"code": "abc", "n": 1}, "bag": {"x": 1, "y": 2}, "reqs": {"a": "s"},
    "ordered": {"a": "s", "b": 1}, "shape": {"r": 1}, "mix": {"a": "s", "b": 1}, "both": [1, 2],
    "both2": 5, "num": 1.5, "nothing": null
  })");
  // комбинации oneOf/anyOf/allOf/not друг в друге и в массивах; каждая ветвь anyOf отдельно
  for (std::string_view any :
       {R"([{"a": "s"}, {"b": 1}, {"a": "t", "b": 2}])", R"({"k": "v"})", R"({"r": 1})", "7"}) {
    check_json_with_mutations<Combo>(std::format(R"({{
      "shapes": ["s", 1, {{"r": 1}}, {{"w": 2}}],
      "pets": [{{"petType": "kitty", "lives": 9}}, {{"bark": true, "petType": "Dog"}}],
      "any": {}, "both": 7, "tree": {{"children": [{{"children": []}}]}}
    }})",
                                                 any));
  }
  // форматы пользователя (from_json_value)
  check_json_with_mutations<Formats>(
      R"({"day": "2026-01-02", "count": 1, "ratio": 0.5, "days": ["2026-01-03"]})");

  // то, чего мутации не дают
  for (std::string_view json : {
           "{\"name\": \"a\"}\n ",                                         // пробелы в конце
           R"({"name": "a"} {"name": "b"})",                               // данные после значения
           R"({"name": "a", "name": "b", "tags": ["x"], "tags": ["y"]})",  // повторяющиеся ключи
           R"({"name": "a", "bag": {"x": 1, "x": 2}})",                    // и в additionalProperties
           R"({"name": "a", "pet": {"petType": "Dog", "bark": true, "bark": false}})",  // и в oneOf
           R"({"name": "a", "reqs": {"x": "y", "a": "s", "b": "not int"}})",  // разбор важнее требования
           R"({"name": "a", "tree": {"children": [{"children": [], "x": 1}]}, "reqs": {"x": "y"}})",
       })
    check_json<Item>(json);
  // требование нарушено раньше по тексту (validate_error на a), чем разбор (parse_error на b): как в DOM,
  // ошибка разбора
  check_json<Ordered>(R"({"a": "long", "b": "x"})");
  check_json<Reqs>(R"({"x": "y", "b": -1})");
  // обе части allOf не подходят: первая - parse_error (не целое), вторая - validate_error (maxItems); ошибка
  // - первой по порядку
  check_json<Both>(R"([1, 2, 3, "x"])", json_check_e::exhaustive);
  // дискриминатор: не первым, нет, неизвестное значение, не строка, повторяется (побеждает последний), не
  // объект
  for (std::string_view pet :
       {R"({"lives": 9, "petType": "kitty"})", R"({"bark": true})", R"({"petType": "Cow"})",
        R"({"petType": 1})", R"({"petType": "Dog", "petType": "kitty", "lives": 1})",
        R"({"petType": "Dog", "bark": "loud"})", R"("Dog")"}) {
    check_json<Pet>(pet, json_check_e::exhaustive);
    check_json<AnyPet>(pet, json_check_e::exhaustive);
  }
  // not как тип не сериализуется: только совпадение итогов
  for (std::string_view not_str : {"1", R"("s")", "{}", R"({"a": "s"})"})
    check_json<NotStr>(not_str, json_check_e::exhaustive);
}

// ошибка разбора тела по ходу прихода прерывает запрос и приходит исключением, соединение живо
TEST("broken JSON response") {
  test_server server(hidi::server_ssl_context_ptr(nullptr));
  bool thrown = false;
  api::getItem::result_t after;
  run_with_server(server, [&](hidi::h2client& h2) -> dd::task<void> {
    api::client cli{client_ref(&h2)};
    api::getItem get;
    get.path.id.get() = 13;
    try {
      (void)co_await cli.send(get, hidi::deadline_t::never());
    } catch (svarg::parse_error&) {
      thrown = true;
    }
    get.path.id.get() = 7;
    after = co_await cli.send(get, hidi::deadline_t::never());
  });
  REQUIRE(thrown && after.get_if_200());
}

TEST("parameter styles") {
  api::getStyles op;
  op.path.id = 5;
  op.path.lab = {1, 2};
  op.path.arr = {1, 2};
  op.path.mat = {1, 2};
  op.path.obj.a = 1;
  op.path.obj.b = 2;
  op.query.sp = {"x", "y"};
  op.query.pp = {"x", "y"};
  op.query.deep.emplace().a = 1;
  op.query.deep->b = "z";
  http_request req = make_request(op);
  REQUIRE(req.path == "/styles/;id=5/.1.2/.1,2/;mat=1;mat=2/;a=1;b=2?deep[a]=1&deep[b]=z&pp=x|y&sp=x%20y");

  api::getStyles back;
  REQUIRE(!from_request(req, back, "").failed());
  REQUIRE(back.path.id == 5 && back.path.lab == op.path.lab && back.path.arr == op.path.arr &&
          back.path.mat == op.path.mat);
  REQUIRE(back.path.obj.a == 1 && back.path.obj.b == 2);
  REQUIRE(back.query.sp == op.query.sp && back.query.pp == op.query.pp);
  REQUIRE(back.query.deep && back.query.deep->a == 1 && back.query.deep->b == "z");

  // разделители, закодированные по-другому
  req.path = "/styles/;id=5/.1.2/.1,2/;mat=1;mat=2/;a=1;b=2?deep%5Ba%5D=1&pp=x%7Cy&sp=x+y";
  api::getStyles alt;
  REQUIRE(!from_request(req, alt, "").failed());
  REQUIRE(alt.query.sp == op.query.sp && alt.query.pp == op.query.pp && alt.query.deep->a == 1);
  // label без точки, matrix без имени - ошибка
  req.path = "/styles/;id=5/1.2/.1,2/;mat=1;mat=2/;a=1;b=2";
  REQUIRE(from_request(req, alt, "").failed());
  req.path = "/styles/5/.1.2/.1,2/;mat=1;mat=2/;a=1;b=2";
  REQUIRE(from_request(req, alt, "").failed());
}

TEST("required nullable field") {
  api::components::schemas::Nulls n;
  REQUIRE(svarg::to_json(n) == boost::json::parse(R"({"a": null})"));  // b не required
  n.a = "x";
  REQUIRE(svarg::to_json(n) == boost::json::parse(R"({"a": "x"})"));
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"({"a": null})"), n).failed() && !n.a);
}

TEST("required recursive field") {
  api::components::schemas::Tree t;
  REQUIRE(!svarg::try_from_json(boost::json::parse(R"({"children": [{"children": []}]})"), t).failed());
  REQUIRE(t.children->size() == 1 && (*t.children)[0].children->empty());
  REQUIRE(svarg::to_json(t) == boost::json::parse(R"({"children": [{"children": []}]})"));
  REQUIRE(svarg::try_from_json(boost::json::parse("{}"), t).failed());
}

TEST("discriminator") {
  using api::components::schemas::AnyPet;
  using api::components::schemas::Cat;
  using api::components::schemas::Dog;
  using api::components::schemas::Pet;
  auto parse = [](auto& v, std::string_view json) {
    return !svarg::try_from_json(boost::json::parse(json), v).failed();
  };
  Pet p;
  REQUIRE(parse(p, R"({"petType": "kitty", "lives": 9})"));
  REQUIRE(get_special_field(p).get_if<Cat>() && get_special_field(p).get_if<Cat>()->lives == 9);
  REQUIRE(parse(p, R"({"petType": "Dog"})"));
  REQUIRE(get_special_field(p).get_if<Dog>());
  REQUIRE(!parse(p, R"({"petType": "Cat"})"));  // есть в mapping под другим значением
  REQUIRE(!parse(p, R"({"lives": 9})"));        // нет дискриминатора
  // при сериализации значение дискриминатора выставляется по типу
  Cat cat;
  cat.petType = "x";
  p = Pet{};
  get_special_field(p).emplace<Cat>(cat);
  REQUIRE(svarg::to_json(p).as_object().at("petType") == "kitty");

  AnyPet a;
  REQUIRE(parse(a, R"({"petType": "Cat", "bark": true})"));
  REQUIRE(get_special_field(a).get<Cat>() && !get_special_field(a).get<Dog>());
  REQUIRE(!parse(a, R"({"petType": "kitty"})"));
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
          }())
              .body.strview() == "user=u&remember=false&meta=%22m%22");
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
  // значения пути закодированы: '/' и разделитель '.' внутри значения
  api::getFile file;
  file.path.name = "a/b.c d%+";
  file.path.ext = "tar.gz";
  http_request file_req = make_request(file);
  REQUIRE(file_req.path == "/files/a%2Fb%2Ec%20d%25+.tar.gz");
  file.path.name = "id-1@x";  // кодировать нечего
  REQUIRE(make_request(file).path == "/files/id-1@x.tar.gz");
  http_request file_raw;
  file_raw.method = hidi::http_method_e::GET;
  file_raw.path = "/v1/files/x/y.txt";  // '/' как есть тоже принимается (как {ref} у GitHub)
  http_request file_ok = file_raw;
  file_ok.path = "/v1/files/x%2fy.txt";
  // style/explode
  api::search search;
  search.query.ids = {1, 2};
  search.query.tags = {"a", "b,c"};
  search.query.filter.emplace().color = "red";
  search.query.point.emplace().x = 5;
  search.headers.x_list = {"h1", "h2"};
  search.cookies.prefs = {"p1", "p2"};
  http_request search_req = make_request(search);
  REQUIRE(search_req.path == "/search?filter=color,red&ids=1&ids=2&x=5&tags=a,b%2Cc");
  api::getCell cell;
  cell.path.cell.row = 1;
  cell.path.cell.col = 2;
  cell.path.tags.get() = {"x.y", "z,w"};
  http_request cell_req = make_request(cell);
  REQUIRE(cell_req.path == "/cells/col=2,row=1/x.y,z%2Cw");
  http_request cell_bad = with_base_path(cell_req, "/v1");
  cell_bad.path = "/v1/cells/row=1,col=2/";  // minItems (порядок свойств при разборе не важен)
  http_request search_raw;
  search_raw.method = hidi::http_method_e::GET;
  search_raw.path = "/v1/search?ids=7&y=3";  // у point обязателен x
  // свои кодеки: тело по суффиксу +kv (media_type_base), ответ по точному медиа типу
  api::postKv kv;
  kv.body.a = "hi";
  kv.body.n = 3;
  http_request kv_req = with_base_path(make_request(kv), "/v1");
  REQUIRE(kv_req.body.strview() == "a=hi;n=3");
  std::string_view item_json =
      R"({"name":"n","child":{"name":"c","color":"red"},"color":"purple","filter":{"kind":"k","id":"1"}})";
  put_raw.body.data.assign(item_json.begin(), item_json.end());

  test_server server(hidi::server_ssl_context_ptr(nullptr));
  // свои маршруты рядом с операциями спеки: литерал и "ДО" выигрывают у шаблона /v1/items/{id}
  server.routes().add_route(hidi::http_method_e::GET, "/v1/items/custom", test_server::user_route_begin);
  server.routes().add_before(hidi::http_method_e::GET, "/v1/items/{id}.json",
                             test_server::user_route_begin + 1);
  http_request custom_lit = with_base_path(make_request(get), "/v1");
  custom_lit.path = "/v1/items/custom";
  http_request custom_before = custom_lit;
  // последний параметр забирает остаток пути с '/' как есть, так шлют клиенты GitHub
  http_request cell_tail = cell_bad;
  cell_tail.path = "/v1/cells/row=1,col=2/a/b";
  custom_before.path = "/v1/items/5.json";
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
                                          with_base_path(file_req, "/v1"),
                                          file_raw,
                                          file_ok,
                                          with_base_path(search_req, "/v1"),
                                          with_base_path(cell_req, "/v1"),
                                          cell_bad,
                                          search_raw,
                                          kv_req,
                                          custom_lit,
                                          custom_before,
                                          cell_tail,
                                      });
  REQUIRE(rsps.size() == 24);

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
  // открытый enum: любая строка, известные значения - константы (value совпало с членом - value_)
  using api::components::schemas::Color;
  REQUIRE(echoed.body.color && echoed.body.color->value == "purple" && !echoed.body.color->is_known());
  REQUIRE(echoed.body.child->color == Color::red && echoed.body.child->color->is_known());
  static_assert(Color::value_ == "value" && Color::values.size() == 3);
  // требования not и anyOf из схем только с required выполнены
  REQUIRE(echoed.body.filter && echoed.body.filter->kind == "k" && echoed.body.filter->id == "1");
  Color c = Color::green;
  c = "any";
  REQUIRE(c == "any");
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

  REQUIRE(rsps[13].status == 200);
  REQUIRE(rsps[13].body_strview() == "a/b.c d%+|tar.gz");
  REQUIRE(rsps[14].status == 200);
  REQUIRE(rsps[14].body_strview() == "x/y|txt");
  REQUIRE(rsps[15].status == 200);
  REQUIRE(rsps[15].body_strview() == "x/y|txt");

  REQUIRE(rsps[16].status == 200);
  REQUIRE(rsps[16].body_strview() == "ids=1 2 tags=a b,c filter=red/-1 point=5/-1 list=h1 h2 prefs=p1 p2");
  REQUIRE(rsps[17].status == 200);
  REQUIRE(rsps[17].body_strview() == "1:2 x.y z,w");
  REQUIRE(rsps[18].status == 400);
  REQUIRE(rsps[19].status == 400);

  REQUIRE(rsps[20].status == 200);
  REQUIRE(rsps[20].body_strview() == "HI-3");
  api::postKv::response_200 kv_rsp;
  REQUIRE(!from_response(rsps[20], kv_rsp).failed());
  REQUIRE(kv_rsp.body == "hi-3");

  REQUIRE(rsps[21].status == 200 && rsps[21].body_strview() == "user0 /v1/items/custom");
  REQUIRE(rsps[22].status == 200 && rsps[22].body_strview() == "user1 /v1/items/5.json");
  REQUIRE(rsps[23].status == 200 && rsps[23].body_strview() == "1:2 a/b");

  // поле типа самого объекта
  REQUIRE(echoed.body.child && echoed.body.child->name == "c" && !echoed.body.child->child);
}

// middleware: считает запросы и передаёт дальше
struct counting {
  any_client next;
  int* count;

  dd::task<int> send_request(hidi::client_request_context& ctx, hidi::on_header_fn_ptr h,
                             hidi::on_data_part_fn_ptr d, http_request req, hidi::deadline_t dl) {
    ++*count;
    return next.send_request(ctx, h, d, std::move(req), dl);
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
  api::getBlob::result_t blob, no_blob;
  api::getEvents::result_t events;
  std::string blob_data, events_data, chat_data;
  api::getChat::result_t chat_whole, chat_streamed;
  int blob_parts = 0;
  api::putBlob::result_t put_blob;

  run_with_server(server, [&](hidi::h2client& h2) -> dd::task<void> {
    api::client cli{counting{any_client(client_ref(&h2)), &count}};
    api::client_upload_example_com_v2 upload_cli{client_ref(&h2)};

    api::getItem get;
    get.path.id.get() = 7;
    get.headers.x_request_id = "r1";
    item = co_await cli.send(get, hidi::deadline_t::never());

    api::upload up;
    up.body.get() = "file";
    uploaded = co_await upload_cli.send(up, hidi::deadline_t::never());

    // несколько тел: выбирается по content type
    api::addNote note;
    note.body.emplace<api::addNote::application_json_body>().get() = "text";
    (void)co_await cli.send(note, hidi::deadline_t::never());
    note.body.emplace<api::addNote::application_vnd_note_json_body>().get() = 42;
    (void)co_await cli.send(note, hidi::deadline_t::never());

    api::login login;
    login.body.user = "u&=";
    login.body.remember = true;
    login.body.meta = "x y";
    greeting = co_await cli.send(login, hidi::deadline_t::never());

    api::uploadFiles files;
    files.body.title = "t \"q\"";
    files.body.meta.emplace().name = "m";
    files.body.file = std::string("\r\n--\0", 5);
    summary = co_await cli.send(files, hidi::deadline_t::never());

    // ответы потоком: тело потокового ответа (выбирается по статусу) отдаётся в колбэк по мере прихода,
    // остальные ответы разбираются как обычно
    std::set<std::pair<int, std::string>> blob_streams;
    auto on_blob = [&](int status, std::string_view content_type, std::span<const hidi::byte_t> part, bool) {
      blob_streams.emplace(status, content_type);
      blob_data.append((const char*)part.data(), part.size());
      ++blob_parts;
    };
    api::getBlob get_blob;
    get_blob.path.name = "b";
    blob = co_await cli.send(get_blob, &on_blob, hidi::deadline_t::never());
    get_blob.path.name = "missing";
    no_blob = co_await cli.send(get_blob, &on_blob, hidi::deadline_t::never());  // 404: JSON разобран
    REQUIRE(blob_streams == std::set<std::pair<int, std::string>>{{200, "application/octet-stream"}});

    // хедеры потокового ответа типизированы и разобраны
    auto on_events = [&](int, std::string_view, std::span<const hidi::byte_t> part, bool) {
      events_data.append((const char*)part.data(), part.size());
    };
    events = co_await cli.send(api::getEvents{}, &on_events, hidi::deadline_t::never());

    // у одного статуса JSON и поток: тело выбирается по content-type
    auto on_chat = [&](int status, std::string_view content_type, std::span<const hidi::byte_t> part, bool) {
      REQUIRE(status == 200);
      REQUIRE(content_type == "text/event-stream");
      chat_data.append((const char*)part.data(), part.size());
    };
    api::getChat chat;
    chat_whole = co_await cli.send(chat, &on_chat, hidi::deadline_t::never());
    chat.query.stream = true;
    chat_streamed = co_await cli.send(chat, &on_chat, hidi::deadline_t::never());

    api::putBlob put;
    put.path.name = "b";
    put.body = std::string("\0\x01\x02", 3);
    put_blob = co_await cli.send(put, hidi::deadline_t::never());

    // статус не из спецификации (404 на неверный base path): у getItem есть default ответ,
    // у upload нет - исключение
    api::client wrong_path{client_ref(&h2), "/nope"};
    not_found = co_await wrong_path.send(get, hidi::deadline_t::never());
    api::client_upload_example_com_v2 wrong_upload{client_ref(&h2), "/nope"};
    try {
      (void)co_await wrong_upload.send(up, hidi::deadline_t::never());
    } catch (unexpected_response& e) {
      unexpected = std::move(e.response);
    }
  });

  REQUIRE(count == 11);
  REQUIRE(blob.get_if_200());
  std::string expected_blob;
  for (int i = 0; i < 1000; ++i)
    expected_blob += std::string("b\0\xff", 3);
  REQUIRE(blob_data == expected_blob);
  REQUIRE(blob_parts > 1);
  REQUIRE(no_blob.get_if_404() && no_blob.get_if_404()->body == "no missing");
  REQUIRE(events.headers.x_count == 3);
  REQUIRE(events_data == "data: x\n\ndata: x\n\ndata: x\n\n");
  using chat_rsp = api::getChat::response_200;
  REQUIRE(std::get<chat_rsp::application_json_body>(chat_whole.body).get() == "whole");
  REQUIRE(std::holds_alternative<chat_rsp::text_event_stream_body>(chat_streamed.body));
  REQUIRE(chat_data == "data: y\n\ndata: y\n\n");
  REQUIRE(put_blob.body == 3);
  REQUIRE(greeting.body == "u&=/true/-/x y");
  REQUIRE(summary.body == std::format("t \"q\"/-1/m/{}", std::string("\r\n--\0", 5)));
  REQUIRE(server.notes == std::vector<std::string>{"text", "42"});
  REQUIRE(item.get_if_200() && item.get_if_200()->body == "item7/10/all");
  REQUIRE(server.uploaded == "file");
  REQUIRE(server.request_id == "r1");  // X-Request-Id из спеки: поле и хедер в нижнем регистре
  REQUIRE(not_found.get_if_default() && not_found.get_if_default()->status == 404);
  REQUIRE(unexpected && unexpected->status == 404);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
