#include <moko3/moko3.hpp>

#include "svarg/backend/cxx/support/common_types.hpp"

// операция повторяет то, что генерирует бекенд: структуры параметров без parse_tag,
// сначала обязательные поля, затем опциональные

using namespace svarg::cxx_support;

struct name_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::fundamental;

  std::string _implfield;

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{.min_length = 2u, .pattern = "^[a-z]+$"},
  }};
};

struct even_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::fundamental;

  integer _implfield;

  static constexpr std::array<constraints_t, 1> constraints = {{
      constraints_t{.multiple_of = bound_t(int64_t(2))},
  }};
};

struct color_t {
  static constexpr parse_tag_e parse_tag = parse_tag_e::string_enum;

  enum struct values_e { red, green };
  using enum values_e;

  static constexpr std::array<std::string_view, 2> values = {"red", "green"};

  values_e _implfield;
};

// GET /v1/{id}/items/{n}
struct get_item {
  static constexpr parse_tag_e parse_tag = parse_tag_e::operation;

  static constexpr inline std::string_view method = "GET";

  static constexpr void visit_path(auto&& vtor) {
    vtor.substr("/v1/");
    vtor.param(std::index_sequence<0>{}, "/items/");
    vtor.param(std::index_sequence<1>{});
  }
  static constexpr std::string_view path_expr = "/v1/{id}/items/{n}";

  struct path_t {
    name_t id;
    even_t n;

    using additional_properties_type = void;

    static constexpr std::array<field_info, 2> fields = {
        {{.name = "id", .required = true}, {.name = "n", .required = true}}};
  };
  struct query_t {
    name_t q;
    std::optional<std::string> s;
    std::optional<color_t> c;

    using additional_properties_type = void;

    static constexpr std::array<field_info, 3> fields = {
        {{.name = "q", .required = true}, {.name = "s"}, {.name = "c"}}};
  };
  struct headers_t {
    even_t x_even;
    std::optional<color_t> x_color;

    using additional_properties_type = void;

    static constexpr std::array<field_info, 2> fields = {
        {{.name = "x-even", .required = true}, {.name = "x-color"}}};
  };
  struct cookies_t {
    std::optional<std::string> a;
    std::optional<name_t> b;

    using additional_properties_type = void;

    static constexpr std::array<field_info, 2> fields = {{{.name = "a"}, {.name = "b"}}};
  };

  path_t path;
  query_t query;
  headers_t headers;
  cookies_t cookies;
};

struct item_response {
  static constexpr parse_tag_e parse_tag = parse_tag_e::response;

  static constexpr int status = 200;

  struct headers_t {
    name_t x_name;

    using additional_properties_type = void;

    static constexpr std::array<field_info, 1> fields = {{{.name = "x-name", .required = true}}};
  };
  headers_t headers;

  static constexpr std::string_view body_content_type = "application/json";
  static constexpr bool body_is_required = true;
  even_t body;
};

// PUT /name с необязательным телом
struct put_name {
  static constexpr parse_tag_e parse_tag = parse_tag_e::operation;

  static constexpr inline std::string_view method = "PUT";
  static constexpr std::string_view path_expr = "/name";

  static constexpr std::string_view body_content_type = "application/json";
  static constexpr bool body_is_required = false;
  std::optional<name_t> body;

  using result_t = item_response;
};

static get_item make_op() {
  get_item op;
  op.path.id._implfield = "abc";
  op.path.n._implfield = 4;
  op.query.q._implfield = "xy";
  op.query.s = "a&b c";
  op.headers.x_even._implfield = 2;
  op.headers.x_color = color_t{color_t::green};
  op.cookies.b = name_t{"zz"};
  return op;
}

static std::string_view header(const http_request& req, std::string_view name) {
  for (auto& h : req.headers)
    if (h.name() == name)
      return h.value();
  return {};
}

static void set_header(http_request& req, std::string_view name, std::string_view value) {
  for (auto& h : req.headers)
    if (h.name() == name)
      h.hvalue = value;
}

static bool parses(const http_request& req) {
  get_item op;
  return !from_request(req, op).failed();
}

TEST("make_request") {
  http_request req = make_request(make_op());
  REQUIRE(req.path == "/v1/abc/items/4?q=xy&s=a%26b%20c");
  REQUIRE(header(req, "x-even") == "2");
  REQUIRE(header(req, "x-color") == "green");
  // отсутствующая первая кука не оставляет разделителя
  REQUIRE(header(req, "cookie") == "b=zz");
}

TEST("request roundtrip") {
  get_item op = make_op();
  op.cookies.a = "1";
  http_request req = make_request(op);
  REQUIRE(header(req, "cookie") == "a=1; b=zz");

  get_item out;
  REQUIRE(!from_request(req, out).failed());
  REQUIRE(out.path.id._implfield == "abc" && out.path.n._implfield == 4);
  REQUIRE(out.query.q._implfield == "xy" && out.query.s == "a&b c" && !out.query.c);
  REQUIRE(out.headers.x_even._implfield == 2 && out.headers.x_color->_implfield == color_t::green);
  REQUIRE(out.cookies.a == "1" && out.cookies.b->_implfield == "zz");
}

TEST("invalid request") {
  http_request req = make_request(make_op());
  REQUIRE(parses(req));

  http_request r = req;
  r.path = "/v1/abc/items/3?q=xy";  // multipleOf в пути
  REQUIRE(!parses(r));
  r.path = "/v1/abc/items/4?q=XY";  // pattern в query
  REQUIRE(!parses(r));
  r.path = "/v1/abc/items/4?s=x";  // нет обязательного q
  REQUIRE(!parses(r));
  r.path = "/v1/abc/items/4?q=xy&unknown=1";  // лишние аргументы игнорируются
  REQUIRE(parses(r));

  r = req;
  set_header(r, "x-color", "blue");
  REQUIRE(!parses(r));
  r = req;
  set_header(r, "cookie", "b=Z");
  REQUIRE(!parses(r));
}

static bool parses(const http_request& req, put_name& op) {
  op = {};
  return !from_request(req, op).failed();
}

TEST("request body") {
  put_name op;
  op.body.emplace()._implfield = "abc";
  http_request req = make_request(op);
  REQUIRE(req.body.content_type == "application/json");
  REQUIRE(req.body.strview() == R"("abc")");

  put_name out;
  REQUIRE(parses(req, out));
  REQUIRE(out.body && out.body->_implfield == "abc");

  req.body.content_type = "Application/JSON; charset=utf-8";
  REQUIRE(parses(req, out));

  req.body.content_type = "text/plain";
  REQUIRE(!parses(req, out));

  req.body.content_type = "application/json";
  auto set_body = [&](std::string_view b) { req.body.data.assign(b.begin(), b.end()); };
  set_body(R"("A")");  // pattern
  REQUIRE(!parses(req, out));
  set_body(R"("abc)");  // некорректный JSON
  REQUIRE(!parses(req, out));

  // необязательное тело отсутствует
  req = make_request(put_name{});
  REQUIRE(req.body.content_type.empty() && req.body.data.empty());
  REQUIRE(parses(req, out));
  REQUIRE(!out.body);
}

TEST("response") {
  item_response rsp;
  rsp.headers.x_name._implfield = "ok";
  rsp.body._implfield = 6;
  http_response raw = make_response(rsp);
  REQUIRE(raw.status == 200);

  item_response out;
  REQUIRE(!from_response(raw, out).failed());
  REQUIRE(out.headers.x_name._implfield == "ok" && out.body._implfield == 6);

  http_response bad = raw;
  bad.headers.front().hvalue = "NO";
  REQUIRE(from_response(bad, out).failed());

  bad = raw;
  bad.body.clear();  // обязательное тело
  std::erase_if(bad.headers, [](auto& h) { return h.name() == "content-type"; });
  REQUIRE(from_response(bad, out).failed());
}

TEST("visit_field_index") {
  // больше, чем metel::visit_index умеет за раз
  for (size_t i : {0, 127, 128, 255, 256, 299})
    REQUIRE(visit_field_index<300>([]<size_t I>() { return I; }, i) == i);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
