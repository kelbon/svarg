#pragma once

#include <algorithm>
#include <ranges>

namespace svarg {

inline void remove_duplicates(auto& x) {
  std::ranges::sort(x);
  auto [b, e] = std::ranges::unique(x);
  x.erase(b, e);
}

template <typename R>
void append_range(auto& to, R&& rng) {
#if __cpp_lib_containers_ranges >= 202202L
  to.append_range(std::forward<R>(rng));
#else
  // no move iterators etc
  to.insert(to.end(), rng.begin(), rng.end());
#endif
}

}  // namespace svarg
