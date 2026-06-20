#include "svarg/backend/cxx/namespace_strings.hpp"

#include "svarg/backend/cxx/cxx_ident.hpp"

namespace svarg::cxx {

std::string jsonptr2namespace(const json_pointer& jp) {
  std::string result;
  for (std::string_view s : jp.segments()) {
    result += to_cxx_ident(jp_unescape_segment(s));
    result += "::";
  }
  if (!result.empty()) {
    // rm last "::"
    result.pop_back();
    result.pop_back();
  }
  return result;
}

std::string namespace_relative(std::string_view a, std::string_view b) {
  if (b.starts_with("::"))
    b.remove_prefix(2);
  if (a.starts_with("::"))
    a.remove_prefix(2);
  if (b.starts_with(a)) {
    std::string_view res = b;
    res.remove_prefix(a.size());
    if (res.starts_with("::"))
      res.remove_prefix(2);
    return std::string(res);
  }
  return "::" + std::string(b);
}

std::string namespace_relative(const json_pointer& a, const json_pointer& b) {
  return namespace_relative(jsonptr2namespace(a), jsonptr2namespace(b));
}

}  // namespace svarg::cxx
