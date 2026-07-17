#include "svarg/utils/path_tools.hpp"

#include <moko3/moko3.hpp>

#define TEST_MONO(IN, EXPECTED_OUT) REQUIRE(svarg::monomorph_media_type(IN) == EXPECTED_OUT)
#define EXPECT_THROW_MONO(IN)        \
  try {                              \
    svarg::monomorph_media_type(IN); \
    FAIL();                          \
  } catch (svarg::validate_error&) { \
  }

TEST("monomoprhing media types") {
  TEST_MONO("text/html", "text/html");
  TEST_MONO("TEXT/HTML", "text/html");
  TEST_MONO("application/json", "application/json");
  // примеры из rfc https://www.rfc-editor.org/rfc/rfc9110.html#section-8.3.1
  TEST_MONO("text/html;charset=utf-8", "text/html; charset=utf-8");
  TEST_MONO("Text/HTML;Charset=\"utf-8\"", "text/html; charset=utf-8");
  TEST_MONO("text/html; charset=\"utf-8\" ", "text/html; charset=utf-8");
  TEST_MONO("text/html;charset=UTF-8", "text/html; charset=utf-8");
  TEST_MONO("text/html ; charset=utf-8", "text/html; charset=utf-8");
  TEST_MONO("text/html;    charset=utf-8", "text/html; charset=utf-8");
  // неизвестный параметр charsed, регистрозависимое значение
  TEST_MONO("text/html;charsed=UTF-8", "text/html; charsed=UTF-8");

  TEST_MONO("text/html;foo=bar", "text/html; foo=bar");
  TEST_MONO("text/html; Foo=BAR", "text/html; foo=BAR");
  TEST_MONO("text/html;foo=bar;charset=\"UTF-8\"", "text/html; charset=utf-8; foo=bar");

  // пустые параметры
  TEST_MONO("text/html;", "text/html");
  TEST_MONO("text/html; ", "text/html");

  // сортировка параметров
  TEST_MONO("text/html;charset=utf-8;foo=bar", "text/html; charset=utf-8; foo=bar");
  TEST_MONO("TeXt/HtMl;ChArSeT=\"UtF-8\";FoO=BaR", "text/html; charset=utf-8; foo=BaR");
  TEST_MONO("TeXt/HtMl;ChArSeT=\"UtF-8\";FoO=BaR; ", "text/html; charset=utf-8; foo=BaR");
  // не ломает строку в значении параметра
  TEST_MONO("application/multipart; boundary=\"hello wOrLd\"/\"/",
            "application/multipart; boundary=\"hello wOrLd\"/\"/");

  // игнор параметра q
  TEST_MONO("TeXt/HtMl;ChArSeT=\"UtF-8\";FoO=BaR;q=0.001", "text/html; charset=utf-8; foo=BaR");
  TEST_MONO("TeXt/HtMl;ChArSeT=\"UtF-8\";q=0.001;FoO=BaR", "text/html; charset=utf-8; foo=BaR");

  // media type ranges

  TEST_MONO("*/*", "*/*");
  TEST_MONO("*/* ; b=a; a=b", "*/*; a=b; b=a");
  TEST_MONO("*/* ; ", "*/*");
  TEST_MONO("*/*;", "*/*");
  TEST_MONO("*/* ;", "*/*");
  TEST_MONO("*/* ", "*/*");

  TEST_MONO("image/*", "image/*");
  TEST_MONO("image/*;param=p", "image/*; param=p");

  // ошибки

  // между = и значением или именем не должно быть пробелов
  EXPECT_THROW_MONO("text/html ;    charset =    utf-8");
  // нет = и значения
  EXPECT_THROW_MONO("text/html;foo");
  // нет значения
  EXPECT_THROW_MONO("text/html;foo=");
  // нет имени
  EXPECT_THROW_MONO("text/html;=utf-8");
  // хз что
  EXPECT_THROW_MONO("");
  EXPECT_THROW_MONO("/");
  EXPECT_THROW_MONO("/;");
  EXPECT_THROW_MONO("application/json;;");
  // обязано быть type + subtype через /
  EXPECT_THROW_MONO("application");
  EXPECT_THROW_MONO("application;foo=bar");
  // разрешён лишь один /, в токенах нельзя
  EXPECT_THROW_MONO("application/json/my");
  EXPECT_THROW_MONO("/application/json");
}

#define TEST_SPECIALIZED(A, B, CMP) REQUIRE(svarg::compare_media_types(A, B) == CMP);

TEST("more specialized?") {
  using std::strong_ordering;
  TEST_SPECIALIZED("*/*", "*/*", strong_ordering::equal);
  TEST_SPECIALIZED("*/*", "*/*; f=1", strong_ordering::less);
  TEST_SPECIALIZED("*/*; f=1", "*/*", strong_ordering::greater);
  TEST_SPECIALIZED("*/*; a=1; b=2", "*/*; a=1", strong_ordering::greater);
  TEST_SPECIALIZED("*/*; a=1", "*/*; a=1; b=2", strong_ordering::less);

  // === wildcard vs concrete ===
  TEST_SPECIALIZED("*/*", "application/json", strong_ordering::less);
  TEST_SPECIALIZED("application/json", "*/*", strong_ordering::greater);

  TEST_SPECIALIZED("image/*", "image/png", strong_ordering::less);
  TEST_SPECIALIZED("image/png", "image/*", strong_ordering::greater);

  TEST_SPECIALIZED("text/*", "text/plain", strong_ordering::less);
  TEST_SPECIALIZED("text/plain", "text/*", strong_ordering::greater);

  // === same type ===
  TEST_SPECIALIZED("application/json", "application/json", strong_ordering::equal);

  // === parameters increase specificity ===
  TEST_SPECIALIZED("application/json", "application/json; charset=utf-8", strong_ordering::less);
  TEST_SPECIALIZED("application/json; charset=utf-8", "application/json", strong_ordering::greater);

  // === multiple parameters ===
  TEST_SPECIALIZED("text/html; a=1", "text/html; a=1; b=2", strong_ordering::less);
  TEST_SPECIALIZED("text/html; a=1; b=2", "text/html; a=1", strong_ordering::greater);

  TEST_SPECIALIZED("text/html; a=1; b=2", "text/html; a=1; b=2; c=3", strong_ordering::less);
  TEST_SPECIALIZED("text/html; a=1; b=2; c=3", "text/html; a=1; b=2", strong_ordering::greater);

  // === parameter value differences (same keys, different values) ===
  // сравниваются сейчас как строки
  TEST_SPECIALIZED("text/plain; charset=utf-8", "text/plain; charset=iso-8859-1", strong_ordering::greater);
  TEST_SPECIALIZED("text/plain; charset=iso-8859-1", "text/plain; charset=utf-8", strong_ordering::less);

  // === wildcard + parameters ===
  TEST_SPECIALIZED("*/*", "text/plain; charset=utf-8", strong_ordering::less);
  TEST_SPECIALIZED("text/plain; charset=utf-8", "*/*", strong_ordering::greater);

  // === vendor / structured types ===

  // сравниваются как строки
  TEST_SPECIALIZED("application/*+json", "application/vnd.api+json", strong_ordering::less);
  TEST_SPECIALIZED("application/vnd.api+json", "application/*+json", strong_ordering::greater);

  TEST_SPECIALIZED("application/*+json", "application/json", strong_ordering::less);
  TEST_SPECIALIZED("application/json", "application/*+json", strong_ordering::greater);
  TEST_SPECIALIZED("application/*+xml", "application/soap+xml", strong_ordering::less);
  TEST_SPECIALIZED("application/soap+xml", "application/*+xml", strong_ordering::greater);
}

#define TEST_NO_PARAMS(S, EXPECTED_S) REQUIRE(svarg::without_media_type_parameters(S) == EXPECTED_S)

TEST("rm params") {
  // Базовые случаи без параметров
  TEST_NO_PARAMS("*/*", "*/*");
  TEST_NO_PARAMS("text/plain", "text/plain");
  TEST_NO_PARAMS("application/json", "application/json");
  TEST_NO_PARAMS("image/png", "image/png");

  // Пустая строка и мусор
  TEST_NO_PARAMS("", "");
  TEST_NO_PARAMS(";", "");
  TEST_NO_PARAMS("; charset=utf-8", "");

  // Одиночная точка с запятой (с пробелами и без)
  TEST_NO_PARAMS("*/*;", "*/*");
  TEST_NO_PARAMS("*/* ;", "*/*");
  TEST_NO_PARAMS("*/* ; ", "*/*");
  TEST_NO_PARAMS("text/plain;", "text/plain");
  TEST_NO_PARAMS("text/plain ;", "text/plain");
  TEST_NO_PARAMS("text/plain ; ", "text/plain");

  // Один параметр
  TEST_NO_PARAMS("text/plain;charset=utf-8", "text/plain");
  TEST_NO_PARAMS("text/plain; charset=utf-8", "text/plain");
  TEST_NO_PARAMS("text/plain ;charset=utf-8", "text/plain");
  TEST_NO_PARAMS("text/plain ; charset=utf-8", "text/plain");
  TEST_NO_PARAMS("text/html; charset=UTF-8", "text/html");

  // Несколько параметров
  TEST_NO_PARAMS("text/plain; charset=utf-8; boundary=something", "text/plain");
  TEST_NO_PARAMS("multipart/form-data; boundary=----boundary; charset=utf-8", "multipart/form-data");

  // Параметры с кавычками
  TEST_NO_PARAMS("text/plain; charset=\"utf-8\"", "text/plain");
  TEST_NO_PARAMS("multipart/form-data; boundary=\"----boundary 123\"", "multipart/form-data");

  // Пробелы вокруг media type
  TEST_NO_PARAMS(" text/plain", "text/plain");
  TEST_NO_PARAMS(" text/plain; charset=utf-8", "text/plain");

  // Реальные content-type из браузеров/серверов
  TEST_NO_PARAMS("application/x-www-form-urlencoded; charset=UTF-8", "application/x-www-form-urlencoded");
  TEST_NO_PARAMS("multipart/form-data; boundary=----WebKitFormBoundaryABC123", "multipart/form-data");
  TEST_NO_PARAMS("application/json; charset=utf-8", "application/json");
  TEST_NO_PARAMS("text/html; charset=utf-8", "text/html");

  // Wildcard с параметрами
  TEST_NO_PARAMS("*/*; q=0.9", "*/*");
  TEST_NO_PARAMS("text/*; charset=utf-8", "text/*");
}

TEST("match content type") {
  using svarg::match_content_type;

  // Точное совпадение
  REQUIRE(match_content_type("text/plain", "text/plain"));
  REQUIRE(match_content_type("application/json", "application/json"));
  REQUIRE(match_content_type("image/png", "image/png"));
  REQUIRE(match_content_type("multipart/form-data", "multipart/form-data"));

  // Точное несовпадение
  REQUIRE(!match_content_type("text/plain", "text/html"));
  REQUIRE(!match_content_type("application/json", "application/xml"));
  REQUIRE(!match_content_type("image/png", "image/jpeg"));
  REQUIRE(!match_content_type("text/plain", "application/json"));

  // Wildcard */* — совпадает со всем
  REQUIRE(match_content_type("*/*", "text/plain"));
  REQUIRE(match_content_type("*/*", "application/json"));
  REQUIRE(match_content_type("*/*", "image/png"));
  REQUIRE(match_content_type("*/*", "multipart/form-data"));

  // Wildcard text/* — совпадает с подтипами text
  REQUIRE(match_content_type("text/*", "text/plain"));
  REQUIRE(match_content_type("text/*", "text/html"));
  REQUIRE(!match_content_type("text/*", "application/json"));
  REQUIRE(!match_content_type("text/*", "application/text"));

  // Wildcard image/*
  REQUIRE(match_content_type("image/*", "image/png"));
  REQUIRE(match_content_type("image/*", "image/jpeg"));
  REQUIRE(!match_content_type("image/*", "text/plain"));
  REQUIRE(!match_content_type("image/*", "application/json"));

  // Wildcard application/*
  REQUIRE(match_content_type("application/*", "application/json"));
  REQUIRE(match_content_type("application/*", "application/vnd.api+json"));
  REQUIRE(!match_content_type("application/*", "text/plain"));

  // Регистр (RFC 2045: case-insensitive)
  REQUIRE(match_content_type("text/plain", "Text/Plain"));
  REQUIRE(match_content_type("text/plain", "TEXT/PLAIN"));
  REQUIRE(match_content_type("application/json", "application/JSON"));
  REQUIRE(match_content_type("text/*", "text/html"));
  REQUIRE(match_content_type("text/*", "TEXT/HTML"));

  // Нестандартные / vendor типы
  REQUIRE(match_content_type("application/vnd.api+json", "application/vnd.api+json"));
  REQUIRE(match_content_type("application/*", "application/vnd.api+json"));
  REQUIRE(!match_content_type("text/*", "application/vnd.api+json"));

  // x- типы
  REQUIRE(match_content_type("application/x-www-form-urlencoded", "application/x-www-form-urlencoded"));
  REQUIRE(match_content_type("application/*", "application/x-www-form-urlencoded"));
  REQUIRE(match_content_type("*/*", "application/x-www-form-urlencoded"));
  REQUIRE(!match_content_type("text/*", "application/x-www-form-urlencoded"));
}

TEST("starts_with_ci") {
  using svarg::starts_with_ci;

  // Точное совпадение
  REQUIRE(starts_with_ci("text/plain", "text/plain"));
  REQUIRE(starts_with_ci("", ""));

  // Регистр
  REQUIRE(starts_with_ci("Text/Plain", "text/plain"));
  REQUIRE(starts_with_ci("text/plain", "TEXT/PLAIN"));
  REQUIRE(starts_with_ci("TEXT/PLAIN", "text/plain"));
  REQUIRE(starts_with_ci("tExT/pLaIn", "TeXt/PlAiN"));

  // Префикс короче строки
  REQUIRE(starts_with_ci("text/plain; charset=utf-8", "text/plain"));
  REQUIRE(starts_with_ci("application/json", "application/"));
  REQUIRE(starts_with_ci("ABC", "ab"));

  // Пустой префикс — всегда true
  REQUIRE(starts_with_ci("text/plain", ""));
  REQUIRE(starts_with_ci("", ""));

  // Префикс длиннее строки — false
  REQUIRE(!starts_with_ci("text", "text/plain"));
  REQUIRE(!starts_with_ci("", "x"));

  // Несовпадение
  REQUIRE(!starts_with_ci("text/plain", "text/html"));
  REQUIRE(!starts_with_ci("application/json", "text/"));

  // Граничный символ — отличие в последнем байте префикса
  REQUIRE(starts_with_ci("abcd", "abc"));
  REQUIRE(!starts_with_ci("abcd", "abce"));
}

TEST("starts_with_ci_prefix_lowered") {
  using svarg::starts_with_ci_prefix_lowered;

  REQUIRE(starts_with_ci_prefix_lowered("Text/Plain", "text/plain"));
  REQUIRE(starts_with_ci_prefix_lowered("TEXT/PLAIN", "text/plain"));
  REQUIRE(starts_with_ci_prefix_lowered("text/plain; charset=utf-8", "text/plain"));
  REQUIRE(starts_with_ci_prefix_lowered("", ""));
  REQUIRE(starts_with_ci_prefix_lowered("text/plain", ""));

  REQUIRE(!starts_with_ci_prefix_lowered("text/plain", "text/html"));
  REQUIRE(!starts_with_ci_prefix_lowered("text", "text/plain"));
  REQUIRE(!starts_with_ci_prefix_lowered("", "x"));

  // Не-буквы в prefix — работают корректно
  REQUIRE(starts_with_ci_prefix_lowered("text/plain", "text/"));
  REQUIRE(starts_with_ci_prefix_lowered("TEXT/PLAIN", "text/"));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
