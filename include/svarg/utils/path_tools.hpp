#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include "svarg/fundamental_types.hpp"

namespace svarg {

constexpr std::string_view cut_path_segment(std::string_view& path) noexcept {
  auto pos = path.find('/');
  std::string_view segment;
  if (pos != std::string_view::npos) {
    segment = path.substr(0, pos);
    path.remove_prefix(pos + 1);
  } else {
    segment = path;
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

template <typename F>
void visit_path_segments(std::string_view path, F&& callback) {
  if (path.starts_with('/'))
    path.remove_prefix(1);
  if (path.empty())
    return;
  while (!path.empty())
    callback(cut_path_segment(path));
}

// сравниваем так что users/{id} == users/{name}
[[nodiscard]] constexpr inline bool is_equal_paths_template_insensitve(std::string_view a,
                                                                       std::string_view b) noexcept {
  if (a.starts_with('/'))
    a.remove_prefix(1);
  if (b.starts_with('/'))
    b.remove_prefix(1);
  while (!a.empty() && !b.empty()) {
    auto sega = cut_path_segment(a);
    auto segb = cut_path_segment(b);
    // любые шаблонизированные сегменты равны
    if (sega.starts_with('{')) {
      if (!segb.starts_with('{'))
        return false;
    } else {
      if (sega != segb)
        return false;
    }
  }
  return a.empty() == b.empty();
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

inline std::string path_to_name(std::string_view path) {
  std::string result;
  if (!path.starts_with('/'))
    throw parse_error("path must start with /");
  if (path == "/")
    return "root";
  if (path[1] >= '0' && path[1] <= '9')
    result += '_';

  visit_path_segments(path, [&](std::string_view segment) {
    if (segment.starts_with('{')) {
      if (!segment.ends_with('}'))
        throw parse_error("invalid path segment `{}`", segment);
      segment.remove_prefix(1);
      segment.remove_suffix(1);
    }
    if (segment.empty())
      return;
    result += to_upper(segment[0]);
    result += segment.substr(1);
  });

  return result;
}

inline std::string path_to_name(std::string_view path, string_map<std::string>& name2path,
                                string_map<std::string>& path2name) {
  if (auto it = path2name.find(path); it != path2name.end())
    return it->second;

  std::string name = path_to_name(path);

  on_scope_exit {
    path_as_cxx_identifier(name);
    name2path[name] = path;
    path2name[std::string(path)] = name;
  };

  if (!path2name.contains(name))
    return name;
  for (int i = 2;; ++i) {
    // TODO string view rope (типа две строки эмулируют одну строку)
    // TODO ренж строкового представления чисел типа '1' '2' и тд
    auto num = std::to_string(i);
    if (!path2name.contains(name + num)) {
      name += num;
      return name;
    }
  }
}

}  // namespace svarg
