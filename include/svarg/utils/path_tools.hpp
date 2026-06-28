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

inline constexpr char to_upper(char c) {
  if (c >= 'a' && c <= 'z')
    return c - ('a' - 'A');
  return c;
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

}  // namespace svarg
