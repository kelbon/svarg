#pragma once

#include <anyany/anyany.hpp>

namespace svarg {

template <typename Signature>
using fn_cref = aa::cref<aa::call<Signature>>;

template <typename Signature>
using fn_ref = aa::ref<aa::call<Signature>>;

}  // namespace svarg
