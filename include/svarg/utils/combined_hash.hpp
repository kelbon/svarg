#pragma once

#include <boost/container_hash/hash.hpp>

namespace svarg {

struct combined_hash {
  size_t hash = 0;

  operator size_t() const noexcept {
    return hash;
  }

  void add(const auto&... args) {
    (boost::hash_combine(hash, args), ...);
  }
};
// TODO нужно использовать стд хеш если нет буст хеша, в общем принимать хешер
// TODO - использовать где нужно хеш range/unordered_range
template <typename... Args>
[[nodiscard]] size_t hash_combined(const Args&... args) {
  combined_hash hash;
  hash.add(args...);
  return hash;
}

}  // namespace svarg
