#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "svarg/errors.hpp"

#include "svarg/common_types.hpp"

namespace svarg {

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

// `content_type` может быть и медиа ренжом
// precondition: monomorph_media_type(content_type) == content_type
//            && without_media_type_parameters(content_type) == content_type
[[nodiscard]] bool match_content_type(std::string_view candidate, std::string_view content_type);

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

}  // namespace svarg
