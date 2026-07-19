#include "svarg/utils/parse_url.hpp"

#include <moko3/moko3.hpp>

bool test_parse_uri(std::string_view uri, bool invalid = false) {
  try {
    svarg::url_ref p = svarg::parse_absolute_url(uri);
    return p.to_string() == uri;
  } catch (const std::exception&) {
    return invalid;
  }
}

TEST("parse_absolute_url") {
  // валидные
  REQUIRE(test_parse_uri("https://api.example.com/v2"));
  {
    svarg::url_ref uri = svarg::parse_absolute_url("https://api.example.com/v2");
    REQUIRE(uri.scheme == "https");
    REQUIRE(uri.host == "api.example.com");
    REQUIRE(uri.path == "/v2");
  }
  {
    svarg::url_ref uri = svarg::parse_absolute_url("https://user:pass@example.com:8080/path");
    REQUIRE(uri.scheme == "https");
    REQUIRE(uri.host == "example.com");
    REQUIRE(uri.userinfo == "user:pass");
    REQUIRE(uri.get_port() == 8080);
    REQUIRE(uri.path == "/path");
  }
  REQUIRE(test_parse_uri("http://example.com/"));
  REQUIRE(test_parse_uri("https://example.com:8443/v2"));
  REQUIRE(test_parse_uri("HTTP://example.com/v2"));
  REQUIRE(test_parse_uri("https://[::1]:8080/v2"));
  REQUIRE(test_parse_uri("https://[2001:db8::1]/"));
  REQUIRE(test_parse_uri("https://user:pass@example.com/path"));
  REQUIRE(test_parse_uri("https://user:pass@[::1]:8443/v2"));
  REQUIRE(test_parse_uri("https://example.com?a=b"));
  REQUIRE(test_parse_uri("https://example.com:8080?a=b"));
  REQUIRE(test_parse_uri("https://example.com#top"));
  REQUIRE(test_parse_uri("https://[::1]?x=1"));
  REQUIRE(test_parse_uri("https://example.com/v2?x=1#frag"));
  REQUIRE(test_parse_uri("file:///etc/passwd"));
  REQUIRE(test_parse_uri("coap+tcp://example.com/v2"));
  {
    svarg::url_ref uri = svarg::parse_absolute_url("coap+tcp://example.com/v2");
    REQUIRE(uri.scheme == "coap+tcp");
    REQUIRE(uri.host == "example.com");
    REQUIRE(uri.path == "/v2");
  }
  REQUIRE(test_parse_uri("h2c://example.com/"));
  // TODO relative проверить и написать is_relative

  // невалидные / относительные
  REQUIRE(test_parse_uri("/api/v2", true));
  REQUIRE(test_parse_uri("../v2", true));
  REQUIRE(test_parse_uri("v2/sub", true));
  REQUIRE(test_parse_uri("", true));
  REQUIRE(test_parse_uri("//api.example.com/v2", true));
  REQUIRE(test_parse_uri("not_a_scheme:still/a/path", true));
  REQUIRE(test_parse_uri("1http://example.com", true));
  REQUIRE(test_parse_uri("/redirect?next=http://evil.com", true));
  REQUIRE(test_parse_uri("https://[::1", true));
  REQUIRE(test_parse_uri("https:/example.com", true));
  REQUIRE(test_parse_uri("mailto:foo@example.com", true));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
