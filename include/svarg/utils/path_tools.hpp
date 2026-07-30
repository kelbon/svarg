#pragma once

#include <string>
#include <string_view>

#include "svarg/errors.hpp"

#include "svarg/common_types.hpp"
#include "svarg/utils/media_types.hpp"

namespace svarg {

namespace noexport {

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
// игнорирует возможный % енкодинг путей
[[nodiscard]] constexpr std::strong_ordering compare_paths_template_insensitve(std::string_view a,
                                                                               std::string_view b) noexcept {
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
  // позволяет оборвать парсинг раньше, если уже всё готово
  { v.is_done() } -> std::same_as<bool>;
};

template <templated_path_vtor V>
constexpr void visit_templated_path_parts(std::string_view path, V&& visitor) {
  while (!path.empty()) {
    auto open = path.find('{');

    if (open != 0) {
      visitor.substr(path.substr(0, open));  // open==npos -> substr(0, npos) == вся строка
      if (visitor.is_done())
        return;
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
    if (visitor.is_done())
      return;
  }

  visitor.pattern_end();
}

// предполагает что путь валидный. Возвращает true для шаблонизированных путей типа /v1/{name}
// игнорирует возможный % энкодинг, в том числе { или } в виде %xy (нормальные люди так не делают)
[[nodiscard]] constexpr bool is_template_path(std::string_view p) noexcept {
  return p.contains('{');
}

// Сравнивает два (возможно шаблонизированных) пути по тому какой из них более специализирован
// если одинаково специализированы - возвращается compare_paths_template_insensitve(l, r)
// less - 'l' менее специализирован чем 'r'. 'greater' - более специализирован
// equivalent - нет более специализированного
[[nodiscard]] std::strong_ordering compare_paths_specialized(std::string_view l, std::string_view r);

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

// TODO test
[[nodiscard]] constexpr size_t count_in_str(std::string_view str, std::string_view needle) {
  assert(!needle.empty());
  size_t count = 0;
  do {
    auto pos = str.find(needle);
    if (pos == str.npos)
      return count;
    ++count;
    str.remove_prefix(pos + needle.size());
  } while (!str.empty());
  return count;
}
// TODO test

// проверяет что все переменные в пути не пустые и упоминаются ровно один раз
[[nodiscard]] constexpr bool validate_maybe_templated_path_parameters(std::string_view path) {
  struct vtor_t {
    std::string_view p;
    bool failed = false;

    constexpr void substr(std::string_view) const {
    }
    constexpr void pattern_end() const {
    }
    constexpr void param(std::string_view name) {
      assert(!failed);
      assert(!name.empty());
      failed = count_in_str(p, '{' + std::string(name) + '}') != 1;
    }
    constexpr void param(std::string_view name, std::string_view sep) {
      param(name);
      substr(sep);
    }

    bool is_done() const {
      return failed;
    }
    constexpr void error(std::string_view) {
      failed = true;
    }
  };
  vtor_t vtor{path};
  visit_templated_path_parts(path, vtor);
  return !vtor.failed;
}

// TODO совместить с парсингом path параметров?
// игнорирует возможный % енкодинг путей
constexpr bool match_path_pattern(std::string_view path_pattern, std::string_view path) {
  struct vtor_t {
    std::string_view p;
    bool matches = true;

    constexpr void substr(std::string_view str) {
      if (!p.starts_with(str)) {
        matches = false;
        return;
      }
      p.remove_prefix(str.size());
    }
    constexpr void pattern_end() const {
    }
    constexpr void param(std::string_view name) {
      assert(matches);
      assert(!name.empty());
    }
    constexpr void param(std::string_view name, std::string_view sep) {
      auto pos = p.find(sep);
      if (pos == p.npos) {
        matches = false;
        return;
      }
      p.remove_prefix(pos + sep.size());
    }

    constexpr void error(std::string_view) {
      matches = false;
    }

    bool is_done() const {
      return !matches;
    }
  };
  vtor_t vtor{path};
  visit_templated_path_parts(path_pattern, vtor);
  return vtor.matches;
}

// игнорирует возможный % енкодинг путей
template <constexpr_string PathPattern>
constexpr bool match_path_pattern(std::string_view path) {
  if constexpr (!is_template_path(PathPattern))
    return PathPattern == path;
  else
    return match_path_pattern(PathPattern, path);
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

// pre: is_hex_char(c) == true
[[nodiscard]] constexpr unsigned hex2digit(char c) noexcept {
  if (c >= '0' && c <= '9')
    return unsigned(c - '0');
  if (c >= 'a' && c <= 'f')
    return unsigned(c - 'a' + 10);
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  metel::unreachable();
}

// символ который может быть частью шестнадцатеричного числа
[[nodiscard]] constexpr bool is_hex_char(char c) noexcept {
  switch (c) {
    case '0' ... '9':
    case 'a' ... 'f':
    case 'A' ... 'F':
      return true;
    default:
      return false;
  }
}

template <bool WwwUrlencoded, typename O>
O query_decode(std::string_view s, O out) {
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if constexpr (WwwUrlencoded) {
      if (c == '+') {
        *out = ' ';
        ++out;
        continue;
      }
    }
    if (c == '%' && i + 2 < s.size() && is_hex_char(s[i + 1]) && is_hex_char(s[i + 2])) {
      *out = static_cast<char>((hex2digit(s[i + 1]) << 4) | hex2digit(s[i + 2]));
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

// убирает escape последовательности из сегмента пути
// заменяет %ab на соответствующий символ
// возвращает false на некорректном енкодинге или если за %xy скрывалось '/'
// (это неоднозначность для опенапи пути)
[[nodiscard]] bool deescape_percent_encoding(std::string& inout);
// TODO? какое-то оптимизированное сравнение, чтобы сравнивать хендлеры?..

[[nodiscard]] inline std::string deescape_percent_encoding_str(std::string_view in) {
  std::string out{in};
  if (!deescape_percent_encoding(out))
    throw validate_error("invalid percent encoding");
  return out;
}

inline std::string deescape_path(std::string_view path) {
  std::string out;
  visit_path_segments(path, [&](std::string_view v) {
    out += '/';
    out += deescape_percent_encoding_str(v);
  });
  return out;
}

}  // namespace svarg
