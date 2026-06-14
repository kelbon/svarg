#pragma once

#include <charconv>
#include <string_view>
#include <concepts>

namespace svarg {

template <typename Num>
  requires(std::floating_point<Num> || std::integral<Num>)
[[nodiscard]] bool try_from_chars(std::string_view str, Num& num) {
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), num);
  return ec == std::errc{} && ptr == str.data() + str.size();
}

}  // namespace svarg
