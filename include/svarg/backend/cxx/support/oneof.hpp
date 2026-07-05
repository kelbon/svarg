#pragma once

#include <metel/box_union.hpp>

namespace svarg::cxx_support {

template <typename... Types>
using oneof = metel::box_union<Types...>;

}
