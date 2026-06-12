#pragma once

#include <string_view>
#include "svarg/errors.hpp"

namespace svarg {

struct parsed_ref {
  std::string_view file;
  std::string_view json_ptr;
};

// file#jsonptr
[[nodiscard]] inline parsed_ref parse_ref(std::string_view ref) {
  auto pos = ref.find('#');
  if (pos == ref.npos)
    throw parse_error("invalid $ref: `{}`", ref);
  return parsed_ref{.file = ref.substr(0, pos), .json_ptr = ref.substr(pos + 1)};
}

}  // namespace svarg
