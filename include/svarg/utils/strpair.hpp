#pragma once

#include <string_view>
#include <boost/container_hash/hash.hpp>
#include "svarg/utils/combined_hash.hpp"

namespace svarg {

struct strpair {
  std::string_view str1;
  std::string_view str2;

  bool operator==(const strpair&) const = default;
};

}  // namespace svarg

namespace std {

template <>
struct hash<::svarg::strpair> {
  size_t operator()(const ::svarg::strpair& p) const {
    return svarg::hash_combined(p.str1, p.str2);
  }
};

}  // namespace std
