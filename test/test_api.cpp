
#include <moko3/moko3.hpp>

#include <print>

#include "svarg/frontend/openapi/generate_ir.hpp"

#include "svarg/ir/ir_object.hpp"
// TODO дополнить тест ссылками с override которые должны по цепочке создать себе типы
// (+ сверять их значения, что всё распарсилось как надо)
std::unordered_set<std::string> a_jsonptrs{
    "/components/schemas/User",
    "/components/schemas/Address",
    "/components/schemas/UserPatch",
    "/components/schemas/OrderStatusEvent",
    "/paths/~1users~1{userId}/get/parameters/0/schema",
    ("/paths/~1orders/post/callbacks/orderStatus/{$request.body#~1callbackUrl}/post/requestBody/content/"
     "application~1json/schema"),
    "/webhooks/newOrderWebhook/post/requestBody/content/application~1json/schema",
    "/components/responses/NotFound/content/application~1json/schema",
    "/components/requestBodies/OrderCreate/content/application~1json/schema",
    "/components/schemas/User/properties/favoriteItem",
    "/components/schemas/UserPatch/properties/name",
    "/components/schemas/UserPatch/additionalProperties",
    "/components/schemas/Address/properties/street",
    "/components/schemas/Address/properties/city",
    "/components/schemas/User/properties/id",
    "/components/schemas/User/properties/name",
    "/components/schemas/OrderStatusEvent/properties/status",
    "/components/requestBodies/OrderCreate/content/application~1json/schema/properties/itemId",
    "/components/requestBodies/OrderCreate/content/application~1json/schema/properties/quantity",
    "/components/requestBodies/OrderCreate/content/application~1json/schema/properties/callbackUrl",
    "/webhooks/newOrderWebhook/post/requestBody/content/application~1json/schema/properties/orderId",
    "/webhooks/newOrderWebhook/post/requestBody/content/application~1json/schema/properties/event",
    "/components/parameters/LimitParam/schema",
    "/components/headers/X-Request-Id/schema",
    "/paths/~1users~1{userId}/get/responses/200/headers/X-Rate-Limit/schema",
    "/paths/~1users~1{userId}/get/parameters/0/schema/items",
    "/paths/~1users~1{userId}/parameters/0/schema",
};

std::unordered_set<std::string> b_jsonptrs{
    "/components/schemas/Item",
    "/components/schemas/Money",
    "/components/schemas/Order",
    "/components/schemas/Error",
    "/paths/~1items~1{itemId}/get/responses/200/content/application~1json/schema",
    "/paths/~1items~1{itemId}/get/responses/default/content/application~1json/schema",
    "/paths/~1orders~1{orderId}/get/responses/200/content/application~1json/schema",
    "/components/schemas/Item/properties/tags/items",
    "/components/schemas/Item/properties/id",
    "/components/schemas/Item/properties/title",
    "/components/schemas/Item/properties/tags",
    "/components/schemas/Money/properties/amount",
    "/components/schemas/Money/properties/currency",
    "/components/schemas/Order/properties/id",
    "/components/schemas/Order/properties/status",
    "/components/schemas/Error/properties/code",
    "/components/schemas/Error/properties/message",
    "/paths/~1items~1{itemId}/get/parameters/1/schema",
    "/paths/~1orders~1{orderId}/get/parameters/0/schema",
};

TEST("json pointers are correct") {
  using namespace svarg;
  ir::ir_object ir = openapi2ir("./test_api", {"api-a.yaml", "api-b.yaml"});

  std::unordered_set<std::string> gota;
  std::unordered_set<std::string> gotb;

  for (auto&& s : ir.schemas) {
    if (s.spec == "api-a.yaml")
      gota.insert(std::string(s.jsonptr.escaped()));
    else if (s.spec == "api-b.yaml")
      gotb.insert(std::string(s.jsonptr.escaped()));
    else
      FAIL("invalid spec");
  }
  for (auto&& got : gota) {
    if (!a_jsonptrs.contains(got)) {
      std::println("UNEXPECTED A: {}", got);
    }
  }

  for (auto&& got : gotb) {
    if (!b_jsonptrs.contains(got)) {
      std::println("UNEXPECTED B: {}", got);
    }
  }
  for (auto&& expected : a_jsonptrs) {
    if (!gota.contains(expected)) {
      std::println("NOT FOUND A: {}", expected);
    }
  }
  for (auto&& expected : b_jsonptrs) {
    if (!gotb.contains(expected)) {
      std::println("NOT FOUND B: {}", expected);
    }
  }
  REQUIRE(gota == a_jsonptrs);
  REQUIRE(gotb == b_jsonptrs);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
