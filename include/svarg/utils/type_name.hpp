#pragma once
#include <string_view>

#include <anyany/noexport/type_descriptor_details.hpp>

namespace svarg {

template <typename T>
consteval std::string_view type_name() {
  std::string_view v = aa::noexport::n<std::decay_t<T>>();
  if (!v.empty())
    v.remove_suffix(1);  // TODO там в details неочень имя написано, надо исправить
  return v;
}

}  // namespace svarg
