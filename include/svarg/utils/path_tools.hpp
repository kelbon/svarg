#pragma once

#include <string_view>

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

}  // namespace svarg
