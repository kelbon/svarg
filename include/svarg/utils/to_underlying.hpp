#pragma once

#include <type_traits>

namespace svarg {

template <typename T>
[[nodiscard]] constexpr auto to_underlying(T v) noexcept
  requires(std::is_enum_v<T>)
{
  return static_cast<std::underlying_type_t<T>>(v);
}

}  // namespace svarg
