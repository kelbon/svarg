#pragma once

#include <string_view>

#include <strswitch/strswitch.hpp>

#include "svarg/common_types.hpp"
#include "svarg/errors.hpp"
#include "svarg/utils/constexpr_string.hpp"

namespace svarg {

namespace noexport {

constexpr size_t count_star_segments(std::string_view content_type) {
  if (content_type == "*/*")
    return 2;
  // сегмент не может оказаться в середине по грамматике
  // при этом внури сегмента звёздочка оказаться может, поэтому просто count не подойдёт
  if (content_type.ends_with("/*"))
    return 1;
  return 0;
}

// https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.2
//  token          = 1*tchar
//
//  tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//                 / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//                 / DIGIT / ALPHA
//                 ; any VCHAR, except delimiters
constexpr bool is_tchar(char c) noexcept {
  // ALPHA
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    return true;
  // DIGIT
  if (c >= '0' && c <= '9')
    return true;
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

// https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.3
// HTAB (horizontal tab)
// SP (space)
//   OWS            = *( SP / HTAB )
//                 ; optional whitespace
constexpr bool is_qws(char c) noexcept {
  return c == ' ' || c == '\t';
}

constexpr void trim_qws_front(std::string_view& s) {
  while (!s.empty() && is_qws(s.front()))
    s.remove_prefix(1);
}

constexpr void trim_qws_back(std::string_view& s) {
  while (!s.empty() && is_qws(s.back()))
    s.remove_suffix(1);
}

constexpr void trim_qws(std::string_view& s) {
  trim_qws_front(s);
  trim_qws_back(s);
}

constexpr void unquote(std::string_view& s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    s.remove_prefix(1), s.remove_suffix(1);
}

constexpr std::string_view eat_token(std::string_view& in) {
  if (in.empty() || !is_tchar(in.front()))
    throw validate_error("invalid http token `{}`", in);
  const char* p = in.data();
  size_t pos = 0;
  while (!in.empty() && is_tchar(in.front())) {
    ++pos;
    in.remove_prefix(1);
  }
  return std::string_view(p, pos);
}

}  // namespace noexport

[[nodiscard]] constexpr bool is_wildcard_content_type(std::string_view content_type) {
  return noexport::count_star_segments(content_type) > 0;
}

enum struct content_type_e {
  application_json,
  application_problem_json,
  multipart_related,  // TODO реально саппорт, а не просто тут
  unknown,
};

// дефолтный используется при парсинге всяких хедеров, query/cookies и тд
constexpr inline content_type_e default_content_type = content_type_e::application_json;

inline constexpr std::string_view e2str(content_type_e c) noexcept {
  switch (c) {
    case content_type_e::application_json:
      return "application/json";
    case content_type_e::application_problem_json:
      return "application/problem+json";
    case content_type_e::multipart_related:
      return "multipart/related";
    case content_type_e::unknown:
      return "unknown";
  }
  metel::unreachable();
}

inline constexpr bool efromstr(std::string_view str, content_type_e& out) noexcept {
  std::optional c = ss::string_switch<content_type_e>(str)
                        .case_("application/json", content_type_e::application_json)
                        .case_("application/problem+json", content_type_e::application_problem_json)
                        .case_("multipart/related", content_type_e::multipart_related)
                        .or_nullopt();
  if (!c) {
    out = content_type_e::unknown;
    return false;
  }
  out = *c;
  return true;
}

// name -> value мапа параметров типа "boundary" -> "fdsfsdf" для multipart/form-data и так далее
// ordered чтобы создавать одинаковые хедера на всех платформах
using content_type_params = string_map_ci<std::string>;

// преобразует content type и параметры в значение хедера content-type
// не проверяет валидность параметров, имён и их значений для конкретного content_type
[[nodiscard]] std::string media_type_to_hdr_value(content_type_e, const content_type_params&);

// из значения хедера content-type достаёт content_type и параметры
// добавляет в out, а не заменяет
[[nodiscard]] content_type_e media_type_from_hdr_value(std::string_view content_type_value,
                                                       content_type_params& out);

// убирает различия в представлении для разных медиа типов и ренжей
// например application/JSoN; ChaRset: "UtF-8" -> application/json; charset=utf-8
[[nodiscard]] std::string monomorph_media_type(std::string_view);

[[nodiscard]] std::string_view without_media_type_parameters(std::string_view s);

// создаёт порядок медиа типов, в том числе ренжей
// application/json vs application/xml или */* vs application/json
// image/* vs image/png и тд, таким образом что более специализированный выигрывает
// https://www.rfc-editor.org/rfc/rfc9110.html#section-8.3.1
//
// precondition: `a` и `b` прошли `monomorph_media_type`
// возвращает greater если 'a' более специализировано чем 'b' (или иногда просто для правильного порядка, если
// они одного уровня специализированности)
[[nodiscard]] std::strong_ordering compare_media_types(std::string_view a, std::string_view b);

// `content_type` может быть и медиа ренжом
// precondition: monomorph_media_type(content_type) == content_type
//            && without_media_type_parameters(content_type) == content_type
// `candidate` НЕ wildcard
[[nodiscard]] bool match_content_type(std::string_view content_type, std::string_view candidate);

// возвращает `true` если для медиа типа имеет смысл openapi encoding (кодирование разных частей в разных
// форматах)
[[nodiscard]] bool media_type_supports_encodings(std::string_view media_type);

constexpr bool media_type_has_parameters(std::string_view content_type) {
  return content_type.contains(';');
}

template <constexpr_string ContentType>
constexpr bool match_content_type(std::string_view candidate) {
  static_assert(!ContentType.empty() && !noexport::is_qws(ContentType.str().front()) &&
                !noexport::is_qws(ContentType.str().back()));
  static_assert(!media_type_has_parameters(ContentType.str()));
  static_assert(is_lowered(ContentType.str()));

  assert(!is_wildcard_content_type(candidate));

  constexpr size_t starcount = noexport::count_star_segments(ContentType.str());
  if constexpr (starcount == 2) {
    // */*
    return true;
  } else if constexpr (starcount == 1) {
    static_assert(ContentType.str().ends_with("/*"));
    std::string_view prefix = ContentType.str();
    prefix.remove_suffix(2);  // '/*'
    // TODO что-то сделать для проверки что дальше либо ничего либо параметры
    return starts_with_ci_prefix_lowered(candidate, prefix);
  } else if constexpr (starcount == 0) {
    auto ends_or_params = [&c = candidate] {
      // проверка, что кандидат оканчивается сразу после content_type (либо идут параметры)
      // лямбдой отложено после && чтобы не всегда проверять
      // нужно чтобы "application/json" не матчилось с "application/json2" через starts_with
      return c.size() == ContentType.size() ||
             (noexport::is_qws(c[ContentType.size()]) || c[ContentType.size()] == ';');
    };
    return starts_with_ci_prefix_lowered(candidate, ContentType.str()) && ends_or_params();
  } else {
    static_assert(false, "invalid content type");
  }
}

}  // namespace svarg
