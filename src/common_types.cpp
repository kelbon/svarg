#include "svarg/common_types.hpp"

#include "svarg/errors.hpp"

namespace svarg {

std::string_view e2str(parameter_location l) noexcept {
  switch (l) {
    case parameter_location::path:
      return "path";
    case parameter_location::query:
      return "query";
    case parameter_location::header:
      return "header";
    case parameter_location::cookie:
      return "cookie";
  }
  std::unreachable();
}

void efromstr(std::string_view s, parameter_location& out) {
  using enum parameter_location;
  std::optional loc = ss::string_switch<parameter_location>(s)
                          .case_("query", query)
                          .case_("header", header)
                          .case_("path", path)
                          .case_("cookie", cookie)
                          .or_nullopt();
  if (!loc)
    throw parse_error("unknown parameter location `{}`", s);
  out = *loc;
}

}  // namespace svarg
