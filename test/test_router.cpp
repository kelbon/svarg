#include <moko3/moko3.hpp>

#include "svarg/backend/cxx/support/router.hpp"

using namespace svarg::cxx_support;
using hidi::http_method_e;

static bool throws(auto&& f) {
  try {
    f();
  } catch (std::exception&) {
    return true;
  }
  return false;
}

// id найденного маршрута, -1 - не найден
static long find_id(const route_trie& t, std::string_view path) {
  std::optional id = t.find(path);
  return id ? long(*id) : -1;
}

static long find_id(const router& r, std::string_view path, http_method_e m = http_method_e::GET) {
  std::optional id = r.find(m, path);
  return id ? long(*id) : -1;
}

TEST("route_trie: parameters") {
  route_trie t;
  t.add("/items/{id}", 1);
  t.add("/items/{id}/data/{part}", 2);
  t.add("/cells/{x}/", 3);
  REQUIRE(find_id(t, "/items/5") == 1);
  REQUIRE(find_id(t, "/items/5/data/x") == 2);
  REQUIRE(find_id(t, "/items/5/x") == -1);
  REQUIRE(find_id(t, "/items") == -1);
  REQUIRE(find_id(t, "/items/5/") == -1);
  REQUIRE(find_id(t, "/items/a%2Fb") == 1);  // значение не декодируется
  REQUIRE(find_id(t, "/items/") == 1);       // пустое значение проверит разбор параметра
  REQUIRE(find_id(t, "/cells/1/") == 3);
}

TEST("route_trie: literal branch dead end falls back to parameter") {
  route_trie t;
  t.add("/items/special/info", 1);
  t.add("/items/{id}/details", 2);
  REQUIRE(find_id(t, "/items/special/details") == 2);
  REQUIRE(find_id(t, "/items/special/info") == 1);
}

TEST("route_trie: earlier literal segment wins") {
  route_trie t;
  t.add("/a/{x}/b", 1);
  t.add("/a/b/{y}", 2);
  REQUIRE(find_id(t, "/a/b/b") == 2);
  REQUIRE(find_id(t, "/a/c/b") == 1);
}

TEST("route_trie: conflicts, mixed segments, remove") {
  route_trie t;
  t.add("/a/{x}", 1);
  REQUIRE(throws([&] { t.add("/a/{y}", 2); }));
  REQUIRE(throws([&] { t.add("/files/{name}.{ext}", 3); }));
  REQUIRE(throws([&] { t.add("no-slash/{x}", 4); }));
  t.remove("/a/{z}");  // форма та же
  REQUIRE(find_id(t, "/a/1") == -1);
  REQUIRE(t.empty());  // опустевшие узлы удалены
  t.add("/a/{y}", 5);
  REQUIRE(find_id(t, "/a/1") == 5);
}

TEST("router: literal paths win, methods are separate") {
  router r;
  r.add_route(http_method_e::GET, "/items/{id}", 1);
  r.add_route(http_method_e::GET, "/items/special", 2);
  r.add_route(http_method_e::POST, "/x/special", 3);
  r.add_route(http_method_e::GET, "/x/{id}", 4);
  REQUIRE(find_id(r, "/items/special") == 2);
  REQUIRE(find_id(r, "/items/5") == 1);
  REQUIRE(find_id(r, "/x/special", http_method_e::GET) == 4);
  REQUIRE(find_id(r, "/x/special", http_method_e::POST) == 3);
  REQUIRE(find_id(r, "/x/special", http_method_e::PUT) == -1);
  REQUIRE(throws([&] { r.add_route(http_method_e::GET, "/items/special", 5); }));
  REQUIRE(throws([&] { r.add_route(http_method_e::GET, "/files/{name}.{ext}", 6); }));
}

TEST("router: literal, then before, then templates") {
  router r;
  r.add_route(http_method_e::GET, "/files/{id}", 1);
  r.add_before(http_method_e::GET, "/files/{name}.{ext}", 2);
  r.add_route(http_method_e::GET, "/files/list", 3);
  r.add_before(http_method_e::GET, "/files/{name}.json", 4);  // после первого - не выигрывает
  r.add_before(http_method_e::GET, "/repos/{owner}/contents/{path}", 5);
  REQUIRE(find_id(r, "/files/list") == 3);
  REQUIRE(find_id(r, "/files/a.txt") == 2);
  REQUIRE(find_id(r, "/files/a.json") == 2);
  REQUIRE(find_id(r, "/files/abc") == 1);
  REQUIRE(find_id(r, "/files/dir/a.txt") == 2);  // '/' в значении параметра "ДО"
  REQUIRE(find_id(r, "/files/a.txt", http_method_e::POST) == -1);
  REQUIRE(find_id(r, "/repos/me/contents/dir/a.txt") == 5);  // последний параметр - остаток пути
  REQUIRE(throws([&] { r.add_before(http_method_e::GET, "/files/{name}.{ext}", 6); }));
  REQUIRE(throws([&] { r.add_before(http_method_e::GET, "/files/{name}{ext}", 7); }));
  REQUIRE(throws([&] { r.add_before(http_method_e::GET, "files/{name}", 8); }));
}

TEST("router: remove") {
  router r;
  r.add_route(http_method_e::GET, "/a/{x}/b", 1);
  r.add_route(http_method_e::GET, "/lit", 2);
  r.add_before(http_method_e::GET, "/v{version}/info", 3);
  REQUIRE(find_id(r, "/a/1/b") == 1);
  REQUIRE(find_id(r, "/lit") == 2);
  REQUIRE(find_id(r, "/v2/info") == 3);

  r.remove_route(http_method_e::GET, "/a/{x}/b");
  r.remove_route(http_method_e::GET, "/lit");
  r.remove_before(http_method_e::GET, "/v{version}/info");
  r.remove_before(http_method_e::GET, "/nothing");
  REQUIRE(find_id(r, "/a/1/b") == -1);
  REQUIRE(find_id(r, "/lit") == -1);
  REQUIRE(find_id(r, "/v2/info") == -1);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
