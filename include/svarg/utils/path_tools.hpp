#pragma once

#include <string>
#include <string_view>

#include "svarg/errors.hpp"

#include "svarg/common_types.hpp"

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
inline constexpr bool is_tchar(char c) noexcept {
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
inline bool is_qws(char c) noexcept {
  return c == ' ' || c == '\t';
}

inline void trim_qws_front(std::string_view& s) {
  while (!s.empty() && is_qws(s.front()))
    s.remove_prefix(1);
}

inline void trim_qws_back(std::string_view& s) {
  while (!s.empty() && is_qws(s.back()))
    s.remove_suffix(1);
}

inline void trim_qws(std::string_view& s) {
  trim_qws_front(s);
  trim_qws_back(s);
}

inline void unquote(std::string_view& s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    s.remove_prefix(1), s.remove_suffix(1);
}

static std::string_view eat_token(std::string_view& in) {
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

// https://datatracker.ietf.org/doc/html/rfc6265#section-4.1.1
inline constexpr bool is_cookie_octet(char c) noexcept {
  switch (c) {
    case '"':
    case ',':
    case ';':
    case '\\':
      return false;
    default:
      return c > 0x20 && c < 0x7F;  // печатаемый ASCII, не CTL, не SP
  }
}

}  // namespace noexport

[[nodiscard]] constexpr bool is_wildcard_content_type(std::string_view content_type) {
  return noexport::count_star_segments(content_type) > 0;
}

constexpr std::string_view cut_path_segment(std::string_view& path) noexcept {
  if (path.empty()) [[unlikely]]
    return path;
  assert(path.starts_with('/'));
  std::string_view segment;
  // находим два '/' и отдаём то что оказалось между ними
  auto pos = path.find('/', 1);
  if (pos != path.npos) {
    segment = path.substr(1, pos - 1);
    path.remove_prefix(pos);
  } else {
    segment = path.substr(1);
    path = {};
  }
  return segment;
}

// принимает path / json pointer
[[nodiscard]] constexpr std::string_view last_segment(std::string_view path) noexcept {
  auto pos = path.find_last_of('/');
  if (pos == path.npos)
    return path;
  return path.substr(pos + 1);
}

// нарезает сегменты также как для json pointer
// https://datatracker.ietf.org/doc/html/rfc6901#section-5
// т.е. "" - ни разу не вызовет калбек
// "/" - callback("")
// "//" - callback("") дважды
template <typename F>
void visit_path_segments(std::string_view path, F&& callback) {
  if (path.empty())
    return;
  assert(path.starts_with('/'));
  while (!path.empty())
    callback(cut_path_segment(path));
}

// сравниваем так что users/{id} == users/{name}
[[nodiscard]] constexpr inline std::strong_ordering compare_paths_template_insensitve(
    std::string_view a, std::string_view b) noexcept {
  while (!a.empty() && !b.empty()) {
    std::string_view sega = cut_path_segment(a);
    std::string_view segb = cut_path_segment(b);
    bool atem = sega.starts_with('{');
    bool btem = segb.starts_with('{');
    if (atem && btem)
      continue;  // любые шаблонизированные сегменты равны
    if (atem)
      return std::strong_ordering::less;  // шаблонизированный сегмент "меньше" обычного
    else if (btem)
      return std::strong_ordering::greater;
    else {
      std::strong_ordering c = sega <=> segb;
      if (c != std::strong_ordering::equal)
        return c;
    }
  }
  return a.empty() <=> b.empty();
}

[[nodiscard]] constexpr inline bool is_equal_paths_template_insensitve(std::string_view a,
                                                                       std::string_view b) noexcept {
  return compare_paths_template_insensitve(a, b) == std::strong_ordering::equal;
}

// одновременно валидирует что символы валидны для path
// и что path может быть cxx identifier
inline void path_as_cxx_identifier(std::string& path) {
  for (char& c : path) {
    switch (c) {
      case '0' ... '9':
      case 'a' ... 'z':
      case 'A' ... 'Z':
      case '_':
        break;
      case '-':
      case '.':
      case '~':
      case '!':
      case '$':
      case '&':
      case '\'':
      case '(':
      case ')':
      case '*':
      case '+':
      case ',':
      case ';':
      case '=':
      case '@':
      case ':':
      case '/':
      case '%':
      case '{':
      case '}':
        c = '_';
        break;

      default:
        throw parse_error("invalid symbol in path, symbol: {}, url: {}", c, path);
    }
  }
}

std::string path_to_name(std::string_view path);

std::string path_to_name(std::string_view path, string_map<std::string>& name2path,
                         string_map<std::string>& path2name);

// json pointer

[[nodiscard]] inline bool jp_escape_required(std::string_view seg) {
  return seg.find_first_of("~/") != std::string_view::npos;
}

[[nodiscard]] inline std::string jp_escape_segment(std::string_view seg) {
  if (!jp_escape_required(seg))
    return std::string(seg);
  std::string out;
  out.reserve(seg.size());
  for (char c : seg) {
    if (c == '~')
      out += "~0";
    else if (c == '/')
      out += "~1";
    else
      out += c;
  }
  return out;
}

[[nodiscard]] inline bool jp_unescape_required(std::string_view seg) {
  return seg.find('~') != std::string_view::npos;
}

[[nodiscard]] inline std::string jp_unescape_segment(std::string_view seg) {
  if (!jp_unescape_required(seg))
    return std::string(seg);
  std::string out;
  out.reserve(seg.size());
  for (size_t i = 0; i < seg.size(); ++i) {
    if (seg[i] == '~' && i + 1 < seg.size()) {
      if (seg[i + 1] == '0') {
        out += '~';
        ++i;
        continue;
      }
      if (seg[i + 1] == '1') {
        out += '/';
        ++i;
        continue;
      }
    }
    out += seg[i];
  }
  return out;
}

// MEDIA TYPES (content types)

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
std::string media_type_to_hdr_value(content_type_e, const content_type_params&);

// TODO
// TODO написать в доке про allof что там "наследование" зависит от порядка (TODO чтобы этот порядок был
// сверху вниз)
// TODO создать файл доки
// из значения хедера content-type достаёт content_type и параметры
// добавляет в out, а не заменяет
content_type_e media_type_from_hdr_value(std::string_view content_type_value, content_type_params& out);

// убирает различия в представлении для разных медиа типов и ренжей
// например application/JSoN; ChaRset: "UtF-8" -> application/json; charset=utf-8
std::string monomorph_media_type(std::string_view);

[[nodiscard]] std::string_view without_media_type_parameters(std::string_view s);

// создаёт порядок медиа типов, в том числе ренжей
// application/json vs application/xml или */* vs application/json
// image/* vs image/png и тд, таким образом что более специализированный выигрывает
// https://www.rfc-editor.org/rfc/rfc9110.html#section-8.3.1
//
// precondition: `a` и `b` прошли `monomorph_media_type`
// возвращает greater если 'a' более специализировано чем 'b' (или иногда просто для правильного порядка, если
// они одного уровня специализированности)
std::strong_ordering compare_media_types(std::string_view a, std::string_view b);

// TODO match_content_type / match_status_range с cosntexpr аргументом

// `content_type` может быть и медиа ренжом
// precondition: monomorph_media_type(content_type) == content_type
//            && without_media_type_parameters(content_type) == content_type
// `candidate` НЕ wildcard
// TODO оптимизированная версия - в хедере, принимает content_type на компиляции, знает что оба lowercase
// и проверяет что кандидат не wildcard
// делает starts_with чтобы не сломаться на параметрах
// TODO ещё пользователю или хотя бы реализации нужны параметры иногда. Для multipart и тд
[[nodiscard]] bool match_content_type(std::string_view content_type, std::string_view candidate);

[[nodiscard]] bool match_status_range(int status, status_range_e r) noexcept;

// возвращает `true` если для медиа типа имеет смысл openapi encoding (кодирование разных частей в разных
// форматах)
[[nodiscard]] bool media_type_supports_encodings(std::string_view media_type);

template <class V>
concept templated_path_vtor = requires(V v, std::string_view param_name, std::string_view error_msg,
                                       std::string_view sv, std::string_view separator) {
  // `sv` может быть пустым
  v.substr(sv);
  // `param_name` никогда не пустое
  v.param(param_name);             // параметр без разделителя (до конца строки)
  v.param(param_name, separator);  // параметр с разделителем (разделитель уже "съеден")
  v.pattern_end();
  v.error(error_msg);
};

template <templated_path_vtor V>
void visit_templated_path_parts(std::string_view path, V&& visitor) {
  while (!path.empty()) {
    auto open = path.find('{');

    if (open != 0) {
      visitor.substr(path.substr(0, open));  // open==npos -> substr(0, npos) == вся строка
      if (open == std::string_view::npos)
        return visitor.pattern_end();
      path.remove_prefix(open);
    }

    auto close = path.find('}');
    if (close == std::string_view::npos)
      return visitor.error("unterminated parameter: missing '}'");

    std::string_view name = path.substr(1, close - 1);
    if (name.empty())
      return visitor.error("empty parameter name '{}'");

    path.remove_prefix(close + 1);

    auto next_open = path.find('{');
    std::string_view sep = path.substr(0, next_open);

    if (sep.empty() && next_open == 0)
      return visitor.error("two parameters in a row without a separator");

    if (sep.empty())
      visitor.param(name);  // конец строки — терминатор не нужен
    else {
      visitor.param(name, sep);
      path.remove_prefix(sep.size());
    }
  }

  visitor.pattern_end();
}

// QUERY

// подходит и для query в :path и для application/x-www-form-urlencoded
// кодирует + и пробел всегда через %
template <typename O>
O query_encode(char c, O out) {
  static constexpr auto is_legit = [](char c) {
    switch (c) {
      case 'A' ... 'Z':
      case 'a' ... 'z':
      case '0' ... '9':
      case '_':
      case '.':
      case '-':
      case '~':
        return true;
      default:
        return false;
    }
  };
  if (is_legit(c)) {
    *out = c;
    ++out;
  } else {
    out = std::format_to(out, "%{:02X}", (unsigned)(unsigned char)c);
  }
  return out;
}

template <typename O>
O query_encode(std::string_view s, O out) {
  for (char c : s)
    out = query_encode(c, out);
  return out;
}

template <bool WwwUrlencoded, typename O>
O query_decode(std::string_view s, O out) {
  static constexpr auto is_hex = [](char c) {
    switch (c) {
      case '0' ... '9':
      case 'a' ... 'f':
      case 'A' ... 'F':
        return true;
      default:
        return false;
    }
  };
  static constexpr auto hex_val = [](char c) -> unsigned {
    if (c >= '0' && c <= '9')
      return unsigned(c - '0');
    if (c >= 'a' && c <= 'f')
      return unsigned(c - 'a' + 10);
    return unsigned(c - 'A' + 10);
  };

  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if constexpr (WwwUrlencoded) {
      if (c == '+') {
        *out = ' ';
        ++out;
        continue;
      }
    }
    if (c == '%' && i + 2 < s.size() && is_hex(s[i + 1]) && is_hex(s[i + 2])) {
      *out = static_cast<char>((hex_val(s[i + 1]) << 4) | hex_val(s[i + 2]));
      ++out;
      i += 2;
    } else {
      // некорректный '%' (не хватает hex-цифр) — пропускаем как есть, игнорируем ошибку
      *out = c;
      ++out;
    }
  }
  return out;
}

// делит путь на сам путь и query (в которой возможно будет фрагмент)
inline std::pair<std::string_view, std::string_view> path_and_query(std::string_view path_hdr) {
  auto pos = path_hdr.find('?');
  if (pos == std::string_view::npos)
    return {path_hdr, std::string_view{}};
  return {path_hdr.substr(0, pos), path_hdr.substr(pos + 1)};
}

// без фрагмента в конце
inline std::string_view get_query(std::string_view path) {
  auto q = path.find('?');
  if (q == std::string_view::npos)
    return {};
  std::string_view query = path.substr(q + 1);

  auto h = query.find('#');
  if (h != std::string_view::npos)
    query = query.substr(0, h);

  return query;
}

// WwwUrlEncoded=true для application/x-www-form-urlencoded и false для query из хедера path
// вызывает vtor(std::string_view name, std::string_view value) для каждой пары
// Пара без '=' даёт пустой value, пустые пары (&<ПУСТО>&) пропускаются
template <bool WwwUrlEncoded>
void visit_query(std::string_view query, auto&& vtor) {
  std::string name_buf, value_buf;  // переиспользуются между итерациями

  while (!query.empty()) {
    auto amp = query.find('&');
    std::string_view pair = query.substr(0, amp);
    query = (amp == std::string_view::npos) ? std::string_view{} : query.substr(amp + 1);
    if (pair.empty())
      continue;

    auto eq = pair.find('=');
    std::string_view raw_name = pair.substr(0, eq);
    std::string_view raw_value = (eq == std::string_view::npos) ? std::string_view{} : pair.substr(eq + 1);

    name_buf.clear();
    value_buf.clear();
    query_decode<WwwUrlEncoded>(raw_name, std::back_inserter(name_buf));
    query_decode<WwwUrlEncoded>(raw_value, std::back_inserter(value_buf));

    vtor(std::string_view{name_buf}, std::string_view{value_buf});
  }
}

// TODO test

// cookie-string = cookie-pair *( ";" SP cookie-pair )
// cookie-pair   = cookie-name "=" cookie-value
// cookie-name   = token
// cookie-value  = *cookie-octet / ( DQUOTE *cookie-octet DQUOTE )
// (RFC 6265, §4.2.1)
[[nodiscard]] error_code visit_cookies(std::string_view header, auto&& vtor) {
  noexport::trim_qws(header);

  while (!header.empty()) {
    size_t semi = header.find(';');
    std::string_view pair = header.substr(0, semi);
    noexport::trim_qws(pair);

    if (pair.empty())
      return parse_error("empty cookie-pair in cookie header");

    std::string_view name;
    try {
      name = eat_token(pair);
    } catch (validate_error& e) {
      return std::move(e);
    }
    if (pair.empty() || pair.front() != '=')
      return parse_error("expected '=' after cookie-name `{}`", name);
    pair.remove_prefix(1);  // '='

    std::string_view value = pair;
    noexport::unquote(value);

    for (char c : value) {
      if (!noexport::is_cookie_octet(c)) [[unlikely]]
        return parse_error("invalid cookie-octet in value of `{}`", name);
    }

    error_code ec = vtor(name, value);
    if (ec)
      return ec;

    if (semi == std::string_view::npos)
      break;
    header.remove_prefix(semi + 1);
    noexport::trim_qws_front(header);
  }

  return {};
}

}  // namespace svarg
