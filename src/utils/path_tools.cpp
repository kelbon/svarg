#include "svarg/utils/path_tools.hpp"

namespace svarg {

std::string path_to_name(std::string_view path) {
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
    result += to_upper_ascii(segment[0]);
    result += segment.substr(1);
  });

  return result;
}

std::string path_to_name(std::string_view path, string_map<std::string>& name2path,
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

bool deescape_percent_encoding(std::string& inout) {
  size_t i = 0;         // позиция текущего анализируемого символа
  size_t writepos = 0;  // позиция куда записывать этот символ
  for (size_t i = 0; i < inout.size();) {
    char c = inout[i];

    if (c != '%') {
      inout[writepos] = inout[i];
      ++writepos;
      ++i;
      continue;
    }

    if (i + 2 >= inout.size()) [[unlikely]]
      return false;
    if (!is_hex_char(inout[i + 1]) || !is_hex_char(inout[i + 2])) [[unlikely]]
      return false;
    unsigned hi = hex2digit(inout[i + 1]);
    unsigned lo = hex2digit(inout[i + 2]);
    inout[i] = static_cast<char>((hi << 4) | lo);
    writepos += 3;
    i += 3;
    if (inout[i] == '/') [[unlikely]]
      return false;
  }
  inout.resize(writepos);
  return true;
}

}  // namespace svarg
