#pragma once
#include <string_view>

#include <anyany/noexport/type_descriptor_details.hpp>

namespace svarg {

template <typename T>
consteval std::string_view type_name() {
  return aa::noexport::n<std::decay_t<T>>();
}

}  // namespace svarg
