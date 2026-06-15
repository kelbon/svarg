#include "svarg/ir/ir_object.hpp"
#include "svarg/ir/serialize.hpp"
#include "svarg/utils/json_pretty.hpp"

#include <moko3/moko3.hpp>

#define EQUAL(X, Y) \
  if (X != Y)       \
  FAIL(std::format("{} != {}", X, Y))

using namespace svarg::ir;

TEST("to_debug_pretty_json_str / schema / fundamental string") {
  schema s;
  s.scope = "spec.yaml#/components/schemas/MyString";
  s.name = "MyString";
  s.data = fundamental_e::string;

  EQUAL(svarg::to_debug_pretty_json_str(s), R"({
  "data" : {
    "fundamental" : "string"
  },
  "name" : "MyString",
  "scope" : "spec.yaml#/components/schemas/MyString"
})");
}

// 1. object_schema с двумя полями, одно required, одно нет
TEST("to_debug_pretty_json_str / schema / object with fields") {
  schema s;
  s.scope = "spec.yaml#/components/schemas/User";
  s.name = "User";

  object_schema obj;
  obj.fields.push_back({
      .type = {.name = "MyString", .scope = "spec.yaml#/components/schemas/MyString"},
      .name = "username",
      .required = true,
  });
  obj.fields.push_back({
      .type = {.scope = "_#integer"},
      .name = "age",
      .required = false,
  });
  s.data = std::move(obj);

  EQUAL(svarg::to_debug_pretty_json_str(s), R"({
  "data" : {
    "object" : {
      "fields" : [
        {
          "name" : "username",
          "required" : true,
          "type" : {
            "name" : "MyString",
            "scope" : "spec.yaml#/components/schemas/MyString"
          }
        },
        {
          "name" : "age",
          "required" : false,
          "type" : {
            "scope" : "_#integer"
          }
        }
      ]
    }
  },
  "name" : "User",
  "scope" : "spec.yaml#/components/schemas/User"
})");
}

// 2. object_schema с additional_fields
TEST("to_debug_pretty_json_str / schema / object with additional_fields") {
  schema s;
  s.scope = "spec.yaml#/components/schemas/StringMap";
  s.name = "StringMap";

  object_schema obj;
  obj.additional_fields = schema_ref{.scope = "_#string"};
  s.data = std::move(obj);

  EQUAL(svarg::to_debug_pretty_json_str(s), R"({
  "data" : {
    "object" : {
      "additional_fields" : {
        "scope" : "_#string"
      },
      "fields" : []
    }
  },
  "name" : "StringMap",
  "scope" : "spec.yaml#/components/schemas/StringMap"
})");
}

// 3. array_schema
TEST("to_debug_pretty_json_str / schema / array") {
  schema s;
  s.scope = "spec.yaml#/components/schemas/IntList";
  s.name = "IntList";

  array_schema arr;
  arr.element = schema_ref{.scope = "_#integer"};
  s.data = std::move(arr);

  EQUAL(svarg::to_debug_pretty_json_str(s), R"({
  "data" : {
    "array" : {
      "element" : {
        "scope" : "_#integer"
      }
    }
  },
  "name" : "IntList",
  "scope" : "spec.yaml#/components/schemas/IntList"
})");
}

// 4. oneof_schema с discriminator
TEST("to_debug_pretty_json_str / schema / oneof with discriminator") {
  schema s;
  s.scope = "spec.yaml#/components/schemas/Shape";
  s.name = "Shape";

  oneof_schema oneof;
  oneof.discriminator = discriminator_desc{
      .propertyName = "kind",
      .mapping =
          {
              {"circle", schema_ref{.name = "Circle", .scope = "spec.yaml#/components/schemas/Circle"}},
              {"rect", schema_ref{.name = "Rect", .scope = "spec.yaml#/components/schemas/Rect"}},
          },
  };
  oneof.schemas = {
      schema_ref{.name = "Circle", .scope = "spec.yaml#/components/schemas/Circle"},
      schema_ref{.name = "Rect", .scope = "spec.yaml#/components/schemas/Rect"},
  };
  s.data = std::move(oneof);

  EQUAL(svarg::to_debug_pretty_json_str(s), R"({
  "data" : {
    "oneof" : {
      "discriminator" : {
        "mapping" : {
          "circle" : {
            "name" : "Circle",
            "scope" : "spec.yaml#/components/schemas/Circle"
          },
          "rect" : {
            "name" : "Rect",
            "scope" : "spec.yaml#/components/schemas/Rect"
          }
        },
        "propertyName" : "kind"
      },
      "schemas" : [
        {
          "name" : "Circle",
          "scope" : "spec.yaml#/components/schemas/Circle"
        },
        {
          "name" : "Rect",
          "scope" : "spec.yaml#/components/schemas/Rect"
        }
      ]
    }
  },
  "name" : "Shape",
  "scope" : "spec.yaml#/components/schemas/Shape"
})");
}

// 5. any_schema + поле required заполнено
TEST("to_debug_pretty_json_str / schema / any with required") {
  schema s;
  s.scope = "spec.yaml#/components/schemas/Anything";
  s.name = "Anything";
  s.data = any_schema{};
  s.add_requirement(schema_ref{.scope = "_#string"});

  EQUAL(svarg::to_debug_pretty_json_str(s), R"({
  "data" : {
    "any" : {}
  },
  "name" : "Anything",
  "required" : [
    {
      "scope" : "_#string"
    }
  ],
  "scope" : "spec.yaml#/components/schemas/Anything"
})");
}

// 1. простой webhook без параметров, один response по статусу
TEST("to_debug_pretty_json_str / operation / webhook minimal") {
  operation op;
  op.spec = "spec.yaml";
  op.path = "";
  op.method = "POST";
  op.responses.insert(response_desc{
      .status = 200,
  });

  EQUAL(svarg::to_debug_pretty_json_str(op), R"({
  "method" : "POST",
  "path" : "",
  "responses" : [
    {
      "status" : 200
    }
  ],
  "spec" : "spec.yaml"
})");
}

// 2. operation с path/query параметрами
TEST("to_debug_pretty_json_str / operation / with parameters") {
  operation op;
  op.spec = "spec.yaml";
  op.path = "/users/{id}";
  op.method = "GET";
  op.id = "getUser";
  op.parameters.emplace();
  op.parameters->push_back({
      .location = svarg::parameter_location::path,
      .name = "id",
      .schema = schema_ref{.scope = "_#string"},
      .required = true,
  });
  op.parameters->push_back({
      .location = svarg::parameter_location::query,
      .name = "verbose",
      .schema = schema_ref{.scope = "_#boolean"},
  });
  op.responses.insert(response_desc{.status = 200});

  EQUAL(svarg::to_debug_pretty_json_str(op), R"({
  "id" : "getUser",
  "method" : "GET",
  "parameters" : [
    {
      "location" : "path",
      "name" : "id",
      "required" : true,
      "schema" : {
        "scope" : "_#string"
      }
    },
    {
      "location" : "query",
      "name" : "verbose",
      "schema" : {
        "scope" : "_#boolean"
      }
    }
  ],
  "path" : "/users/{id}",
  "responses" : [
    {
      "status" : 200
    }
  ],
  "spec" : "spec.yaml"
})");
}

// 3. operation с телом запроса и ответом с телом
TEST("to_debug_pretty_json_str / operation / with request and response body") {
  operation op;
  op.spec = "spec.yaml";
  op.path = "/users";
  op.method = "POST";
  op.bodies.emplace();
  op.bodies->insert(body{
      .contentType = "application/json",
      .schema =
          schema_ref{.name = "CreateUserRequest", .scope = "spec.yaml#/components/schemas/CreateUserRequest"},
  });
  response_desc resp;
  resp.status = 201;
  resp.bodies.emplace();
  resp.bodies->insert(body{
      .contentType = "application/json",
      .schema = schema_ref{.name = "User", .scope = "spec.yaml#/components/schemas/User"},
  });
  op.responses.insert(std::move(resp));

  EQUAL(svarg::to_debug_pretty_json_str(op), R"({
  "bodies" : [
    {
      "contentType" : "application/json",
      "schema" : {
        "name" : "CreateUserRequest",
        "scope" : "spec.yaml#/components/schemas/CreateUserRequest"
      }
    }
  ],
  "method" : "POST",
  "path" : "/users",
  "responses" : [
    {
      "bodies" : [
        {
          "contentType" : "application/json",
          "schema" : {
            "name" : "User",
            "scope" : "spec.yaml#/components/schemas/User"
          }
        }
      ],
      "status" : 201
    }
  ],
  "spec" : "spec.yaml"
})");
}

// 4. operation с statusRange и defaultResponse
TEST("to_debug_pretty_json_str / operation / with status range and default response") {
  operation op;
  op.spec = "spec.yaml";
  op.path = "/items";
  op.method = "GET";
  op.responses.insert(response_desc{.statusRange = status_range_e::_2XX});
  op.defaultResponse = response_desc{
      .statusRange = status_range_e::_5XX,
      .bodies = std::unordered_set<body>{body{
          .contentType = "application/json",
          .schema = schema_ref{.name = "Error", .scope = "spec.yaml#/components/schemas/Error"},
      }},
  };

  EQUAL(svarg::to_debug_pretty_json_str(op), R"({
  "defaultResponse" : {
    "bodies" : [
      {
        "contentType" : "application/json",
        "schema" : {
          "name" : "Error",
          "scope" : "spec.yaml#/components/schemas/Error"
        }
      }
    ],
    "statusRange" : "5XX"
  },
  "method" : "GET",
  "path" : "/items",
  "responses" : [
    {
      "statusRange" : "2XX"
    }
  ],
  "spec" : "spec.yaml"
})");
}

// 5. operation с заголовками в ответе и meta
TEST("to_debug_pretty_json_str / operation / response with headers and meta") {
  operation op;
  op.spec = "spec.yaml";
  op.path = "/files/{fileId}";
  op.method = "GET";
  op.meta.emplace();
  (*op.meta)["_meta_deprecated"] = svarg::Any{boost::json::value{true}};
  response_desc resp;
  resp.status = 200;
  resp.headers.emplace();
  resp.headers->insert(header{
      .name = "X-Request-Id",
      .required = true,
      .schema = schema_ref{.scope = "_#string"},
  });
  resp.headers->insert(header{
      .name = "Content-Disposition",
  });
  op.responses.insert(std::move(resp));

  EQUAL(svarg::to_debug_pretty_json_str(op), R"({
  "meta" : {
    "_meta_deprecated" : true
  },
  "method" : "GET",
  "path" : "/files/{fileId}",
  "responses" : [
    {
      "headers" : [
        {
          "name" : "X-Request-Id",
          "required" : true,
          "schema" : {
            "scope" : "_#string"
          }
        },
        {
          "name" : "Content-Disposition"
        }
      ],
      "status" : 200
    }
  ],
  "spec" : "spec.yaml"
})");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
