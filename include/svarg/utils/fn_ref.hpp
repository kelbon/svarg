#pragma once

#include <anyany/anyany.hpp>

namespace svarg {

template <typename Signature>
using fn_cref = aa::cref<aa::call<Signature>>;

template <typename Signature>
using fn_ref = aa::ref<aa::call<Signature>>;

template <typename Signature>
using move_only_fn = aa::any_with<aa::move, aa::call<Signature>>;

}  // namespace svarg
