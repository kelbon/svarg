#pragma once

#include <anyany/anyany.hpp>

namespace svarg {

template <typename Signature>
using fn_cref = aa::cref<aa::call<Signature>>;

}
