#pragma once

#include <compare>
#include <vector>

#include "svarg/backend/cxx/support/allof.hpp"
#include "svarg/backend/cxx/support/anyof.hpp"
#include "svarg/backend/cxx/support/oneof.hpp"
#include "svarg/backend/cxx/support/not.hpp"
#include "svarg/common_types.hpp"

namespace svarg::cxx_support {

struct null {
  std::strong_ordering operator<=>(const null&) const = default;
};

using integer = int64_t;
using number = ::svarg::number;
using ::svarg::arrayof;
using boolean = bool;

template <typename T>
using additional_properties_t = string_map<T>;

}  // namespace svarg::cxx_support
