#pragma once

#include <compare>
#include <vector>

#include "svarg/backend/cxx/support/allof.hpp"
#include "svarg/backend/cxx/support/anyof.hpp"
#include "svarg/backend/cxx/support/oneof.hpp"
#include "svarg/backend/cxx/support/not.hpp"

namespace svarg::cxx_support {

struct null {
  std::strong_ordering operator<=>(const null&) const = default;
};

using integer = int64_t;
using number = double;
using boolean = bool;

template <typename T>
using arrayof = std::vector<T>;

}  // namespace svarg::cxx_support
