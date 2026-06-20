
#include <moko3/moko3.hpp>

#include <print>

#include "svarg/frontend/openapi/generate_ir.hpp"
#include "svarg/ir/optimization_passes.hpp"

#include "svarg/ir/ir_object.hpp"

// для некоторых схем не генерируется, так как они простые и это ожидаемо
// (тривиальные схемы превращаются сразу в фундаментальную ссылку)
std::unordered_set<std::string> a_jsonptrs{
    "/components/schemas/User",
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
    // сгенерированные через $ref с перекрытым полем
    // т.е. инлайн схемы продублированы, указывает поинтер в объект которого в ямле нет
    "/components/schemas/Order/properties/item/properties/id",
    "/components/schemas/Order/properties/item/properties/tags",
    "/components/schemas/Order/properties/item/properties/title",
    "/components/schemas/Order/properties/item/properties/tags/items",
    "/components/schemas/Order/properties/item",
};
using namespace svarg;

static bool expect_json_ptrs(const ir::ir_object& ir, const std::unordered_set<std::string> aptrs,
                             const std::unordered_set<std::string>& bptrs) {
  std::unordered_set<std::string> gota;
  std::unordered_set<std::string> gotb;

  for (auto&& s : ir.schemas.value()) {
    if (s.spec == "api-a.yaml")
      gota.insert(std::string(s.jsonptr.escaped()));
    else if (s.spec == "api-b.yaml")
      gotb.insert(std::string(s.jsonptr.escaped()));
    else
      return false;
  }
  for (auto&& got : gota) {
    if (!aptrs.contains(got)) {
      std::println("UNEXPECTED A: {}", got);
    }
  }

  for (auto&& got : gotb) {
    if (!bptrs.contains(got)) {
      std::println("UNEXPECTED B: {}", got);
    }
  }
  for (auto&& expected : aptrs) {
    if (!gota.contains(expected)) {
      std::println("NOT FOUND A: {}", expected);
    }
  }
  for (auto&& expected : bptrs) {
    if (!gotb.contains(expected)) {
      std::println("NOT FOUND B: {}", expected);
    }
  }
  return gota == aptrs && gotb == bptrs;
}

TEST("json pointers are correct") {
  // делаем без оптимизаций здесь
  ir::ir_object ir = openapi2ir_raw("./test_api", {"api-a.yaml", "api-b.yaml"});

  REQUIRE(expect_json_ptrs(ir, a_jsonptrs, b_jsonptrs));
}

std::unordered_set<std::string> a_jsonptrs_optimized{
    "/components/schemas/User",
    "/components/schemas/UserPatch",
    "/components/schemas/OrderStatusEvent",
    "/paths/~1users~1{userId}/get/parameters/0/schema",
    ("/paths/~1orders/post/callbacks/orderStatus/{$request.body#~1callbackUrl}/post/requestBody/content/"
     "application~1json/schema"),
    "/webhooks/newOrderWebhook/post/requestBody/content/application~1json/schema",
    "/components/requestBodies/OrderCreate/content/application~1json/schema",
    "/components/schemas/OrderStatusEvent/properties/status",
    "/webhooks/newOrderWebhook/post/requestBody/content/application~1json/schema/properties/event",
    "/paths/~1users~1{userId}/get/parameters/0/schema/items",
};

std::unordered_set<std::string> b_jsonptrs_optimized{
    "/components/schemas/Item",
    "/components/schemas/Money",
    "/components/schemas/Order",
    "/components/schemas/Error",
    "/components/schemas/Item/properties/tags",
    "/components/schemas/Money/properties/currency",
    // сгенерированные через $ref с перекрытым полем
    // т.е. инлайн схемы продублированы, указывает поинтер в объект которого в ямле нет
    "/components/schemas/Order/properties/item/properties/tags",
    "/components/schemas/Order/properties/item",
};

TEST("json pointers are correct (optimized)") {
  ir::ir_object ir = openapi2ir_raw("./test_api", {"api-a.yaml", "api-b.yaml"});
  ir = ir::pass_rm_trivial_schemas(std::move(ir));
  REQUIRE(expect_json_ptrs(ir, a_jsonptrs_optimized, b_jsonptrs_optimized));
}

std::unordered_set<std::string> a_jsonptrs_optimized2{
    "/components/schemas/User",
    "/components/schemas/UserPatch",
    "/paths/~1users~1{userId}/get/parameters/0/schema",
    ("/paths/~1orders/post/callbacks/orderStatus/{$request.body#~1callbackUrl}/post/requestBody/content/"
     "application~1json/schema"),
    "/components/requestBodies/OrderCreate/content/application~1json/schema",
};

std::unordered_set<std::string> b_jsonptrs_optimized2_no_exception{
    "/components/schemas/Order",
    "/components/schemas/Error",
};
std::unordered_set<std::string> b_jsonptrs_optimized2_with_exception{
    "/components/schemas/Order",
    "/components/schemas/Error",
    "/components/schemas/Money/properties/currency",
};

TEST("json pointers are correct (optimized + rm unused)") {
  ir::ir_object ir = openapi2ir_raw("./test_api", {"api-a.yaml", "api-b.yaml"});
  ir = ir::pass_rm_trivial_schemas(std::move(ir));
  ir::ir_object ir2 =
      ir::pass_rm_unused_schemas(ir, {ir::schema_ref{
                                         .spec = "api-b.yaml",
                                         .jsonptr = "/components/schemas/Money/properties/currency"_jp,
                                     }});
  REQUIRE(expect_json_ptrs(ir2, a_jsonptrs_optimized2, b_jsonptrs_optimized2_with_exception));
  ir::ir_object ir3 = ir::pass_rm_unused_schemas(ir);
  REQUIRE(expect_json_ptrs(ir3, a_jsonptrs_optimized2, b_jsonptrs_optimized2_no_exception));
}

TEST("json repr") {
  // std::cout << to_yaml_string(openapi2ir("./test_api", {"api-a.yaml"}));
  // std::cout << to_yaml_string(openapi2ir("./test_api", {"api-b.yaml"}));
  // return;
  ir::ir_object ir = openapi2ir("./test_api", {"api-a.yaml"});
  pass_as_json("./test_api/a_expected.yaml", [&](std::string_view data) {
    boost::json::value j = boost::json::parse(data);
    // крайне сложно сравнивать на разных платформах
    // порядок в массивах + unordered_set/map и то что они получены из порядкового обхода unordered_map и тд
    // REQUIRE(j == to_json(ir));
    auto ir2 = from_json<ir::ir_object>(j);
    REQUIRE(ir2 == ir);
  });
  ir = openapi2ir("./test_api", {"api-b.yaml"});
  pass_as_json("./test_api/b_expected.yaml", [&](std::string_view data) {
    boost::json::value j = boost::json::parse(data);
    auto ir2 = from_json<ir::ir_object>(j);
    REQUIRE(ir2 == ir);
  });
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
