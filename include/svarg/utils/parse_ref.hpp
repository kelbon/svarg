#pragma once

#include <string_view>

#include "svarg/errors.hpp"
#include "svarg/utils/json_pointer.hpp"

namespace svarg {

struct parsed_ref {
  std::string_view file;
  json_pointer json_ptr;
};

// file#jsonptr
[[nodiscard]] inline parsed_ref parse_ref(std::string_view ref) {
  auto pos = ref.find('#');
  if (pos == ref.npos)
    throw parse_error("invalid $ref: `{}`", ref);
  return parsed_ref{.file = ref.substr(0, pos), .json_ptr = json_pointer(ref.substr(pos + 1))};
}

}  // namespace svarg
