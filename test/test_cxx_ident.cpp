#include "svarg/backend/cxx/cxx_ident.hpp"

#include <moko3/moko3.hpp>

#define CXX_IDENT_CASE(IN, EXPECTED_OUTPUT, EXPECTED_SIMPLE) \
  REQUIRE(svarg::cxx::to_cxx_ident(IN) == EXPECTED_OUTPUT);  \
  REQUIRE(svarg::cxx::to_cxx_ident_simple(IN) == EXPECTED_SIMPLE)

TEST("cxx_ident") {
  // Простые идентификаторы — проходят без изменений
  CXX_IDENT_CASE("hello", "hello", "hello");
  CXX_IDENT_CASE("hello_world", "hello_world", "hello_world");
  CXX_IDENT_CASE("myVariable", "myVariable", "myVariable");
  CXX_IDENT_CASE("_private", "_private", "_private");
  CXX_IDENT_CASE("__dunder__", "__dunder__", "__dunder__");

  // Цифра в начале
  CXX_IDENT_CASE("123", "_123", "_123");
  CXX_IDENT_CASE("1abc", "_1abc", "_1abc");
  CXX_IDENT_CASE("0x1F", "_0x1F", "_0x1F");
  CXX_IDENT_CASE("2024_year", "_2024_year", "_2024_year");

  // Спецсимволы из таблицы
  CXX_IDENT_CASE("application/json", "applicationZ2json", "application_json");
  CXX_IDENT_CASE("application-json", "applicationZ0json", "application_json");
  CXX_IDENT_CASE("application_json", "application_json", "application_json");
  CXX_IDENT_CASE("Content-Type", "ContentZ0Type", "Content_Type");
  CXX_IDENT_CASE("foo.bar", "fooZ1bar", "foo_bar");
  CXX_IDENT_CASE("user@example.com", "userZ5exampleZ1com", "user_example_com");
  CXX_IDENT_CASE("some thing", "someZ3thing", "some_thing");
  CXX_IDENT_CASE("a:b", "aZ4b", "a_b");
  CXX_IDENT_CASE("*wildcard*", "Z6wildcardZ6", "_wildcard_");
  CXX_IDENT_CASE("arr[0]", "arrZ70Z8", "arr_0_");

  // Escape-символ Z на входе
  CXX_IDENT_CASE("Zoo", "Z9oo", "Zoo");
  CXX_IDENT_CASE("myZvar", "myZ9var", "myZvar");
  CXX_IDENT_CASE("Z", "Z9", "Z");
  CXX_IDENT_CASE("ZZ", "Z9Z9", "ZZ");
  CXX_IDENT_CASE("ZipZ", "Z9ipZ9", "ZipZ");

  // Ключевые слова C++ — суффикс '_'
  CXX_IDENT_CASE("class", "class_", "class_");
  CXX_IDENT_CASE("int", "int_", "int_");
  CXX_IDENT_CASE("namespace", "namespace_", "namespace_");
  CXX_IDENT_CASE("return", "return_", "return_");
  CXX_IDENT_CASE("co_await", "co_await_", "co_await_");
  // и макросы
  CXX_IDENT_CASE("NULL", "NULL_", "NULL_");
  CXX_IDENT_CASE("linux", "linux_", "linux_");
  CXX_IDENT_CASE("not", "not_", "not_");
  CXX_IDENT_CASE("xor_eq", "xor_eq_", "xor_eq_");
  CXX_IDENT_CASE("true", "true_", "true_");
  CXX_IDENT_CASE("nullptr", "nullptr_", "nullptr_");
  CXX_IDENT_CASE("requires", "requires_", "requires_");

  // Строки, похожие на ключевые слова но не являющиеся ими — без суффикса
  CXX_IDENT_CASE("return/value", "returnZ2value", "return_value");
  CXX_IDENT_CASE("class-name", "classZ0name", "class_name");
  CXX_IDENT_CASE("class_", "class_", "class_");
  CXX_IDENT_CASE("int_", "int_", "int_");
  CXX_IDENT_CASE("integer", "integer", "integer");

  // Предефайненные макросы — суффикс '_'
  CXX_IDENT_CASE("__FILE__", "__FILE___", "__FILE___");
  CXX_IDENT_CASE("__LINE__", "__LINE___", "__LINE___");
  CXX_IDENT_CASE("__cplusplus", "__cplusplus_", "__cplusplus_");
  CXX_IDENT_CASE("__func__", "__func___", "__func___");

  // Спецсимволы вне таблицы — fallback Zxhh
  CXX_IDENT_CASE("$var", "Zx24var", "_var");
  CXX_IDENT_CASE("a=b", "aZx3db", "a_b");
  CXX_IDENT_CASE("a;b", "aZx3bb", "a_b");
  CXX_IDENT_CASE("a&b", "aZx26b", "a_b");
  CXX_IDENT_CASE("a|b", "aZx7cb", "a_b");
  CXX_IDENT_CASE("a+b", "aZx2bb", "a_b");
  CXX_IDENT_CASE("a%b", "aZx25b", "a_b");
  CXX_IDENT_CASE("a#b", "aZx23b", "a_b");
  CXX_IDENT_CASE("a!b", "aZx21b", "a_b");
  CXX_IDENT_CASE("a?b", "aZx3fb", "a_b");
  CXX_IDENT_CASE("a<b", "aZx3cb", "a_b");
  CXX_IDENT_CASE("a>b", "aZx3eb", "a_b");

  // UTF-8 проходит насквозь
  CXX_IDENT_CASE("café", "café", "café");
  CXX_IDENT_CASE("Ñoño", "Ñoño", "Ñoño");
  CXX_IDENT_CASE("日本語", "日本語", "日本語");
  CXX_IDENT_CASE("привет", "привет", "привет");
  CXX_IDENT_CASE("héllo-wörld", "hélloZ0wörld", "héllo_wörld");

  // Реальные юзкейсы: HTTP заголовки
  CXX_IDENT_CASE("X-Request-Id", "XZ0RequestZ0Id", "X_Request_Id");
  CXX_IDENT_CASE("X-Forwarded-For", "XZ0ForwardedZ0For", "X_Forwarded_For");
  CXX_IDENT_CASE("Content-Type", "ContentZ0Type", "Content_Type");
  CXX_IDENT_CASE("Accept-Encoding", "AcceptZ0Encoding", "Accept_Encoding");

  // MIME-типы
  CXX_IDENT_CASE("text/plain", "textZ2plain", "text_plain");
  CXX_IDENT_CASE("multipart/form-data", "multipartZ2formZ0data", "multipart_form_data");
  CXX_IDENT_CASE("application/x-www-form-urlencoded", "applicationZ2xZ0wwwZ0formZ0urlencoded",
                 "application_x_www_form_urlencoded");

  // Пути
  CXX_IDENT_CASE("v1/users/profile", "v1Z2usersZ2profile", "v1_users_profile");
  CXX_IDENT_CASE("/api/v2/", "Z2apiZ2v2Z2", "_api_v2_");
  CXX_IDENT_CASE("../config", "Z1Z1Z2config", "___config");

  // Версии и числовые строки
  CXX_IDENT_CASE("v1.2.3", "v1Z12Z13", "v1_2_3");
  CXX_IDENT_CASE("3.14", "_3Z114", "_3_14");
  CXX_IDENT_CASE("2024-01-15", "_2024Z001Z015", "_2024_01_15");

  // Пустая строка и только спецсимволы
  CXX_IDENT_CASE("", "_", "_");
  CXX_IDENT_CASE("-", "Z0", "_");
  CXX_IDENT_CASE("---", "Z0Z0Z0", "___");
  CXX_IDENT_CASE("-/-", "Z0Z2Z0", "___");
  CXX_IDENT_CASE("...", "Z1Z1Z1", "___");

  // Коллизии — в simple намеренно допускаются, в to_cxx_ident нет
  CXX_IDENT_CASE("a-b", "aZ0b", "a_b");
  CXX_IDENT_CASE("a_b", "a_b", "a_b");  // коллизия в simple
  CXX_IDENT_CASE("aZ0b", "aZ90b", "aZ0b");
  CXX_IDENT_CASE("aZ9b", "aZ99b", "aZ9b");
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
