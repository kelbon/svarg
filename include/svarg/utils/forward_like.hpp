#pragma once

#include <type_traits>

namespace svarg {

// для старых стандартных библиотек
template <typename T, typename U,
          typename Tmp = std::conditional_t<std::is_const_v<std::remove_reference_t<T>>,
                                            const std::remove_reference_t<U>, std::remove_reference_t<U>>>
using forward_like_t = std::conditional_t<std::is_rvalue_reference_v<T&&>, Tmp&&, Tmp&>;

template <typename T, typename U>
constexpr forward_like_t<T, U> forwardLike(U&& x) noexcept {
  return static_cast<forward_like_t<T, U>>(x);
}

}  // namespace svarg
