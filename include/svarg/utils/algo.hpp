#pragma once

#include <algorithm>
#include <ranges>

namespace svarg {

inline void remove_duplicates(auto& x) {
  std::ranges::sort(x);
  auto [b, e] = std::ranges::unique(x);
  x.erase(b, e);
}

}  // namespace svarg
