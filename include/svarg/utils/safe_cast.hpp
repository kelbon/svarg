#pragma once

#include <limits>
#include <stdexcept>

namespace svarg {

template <typename T>
struct numeric_limits {
  static constexpr T min = std::numeric_limits<T>::min();
  static constexpr T max = std::numeric_limits<T>::max();
};

namespace noexport {

constexpr auto unstrongef(auto v) {
  if constexpr (requires { v.value; })
    return v.value;
  else
    return v;
}

constexpr bool cmp_less(auto l, auto r) {
  return std::cmp_less(unstrongef(l), unstrongef(r));
}

constexpr bool cmp_greater(auto l, auto r) {
  return std::cmp_greater(unstrongef(l), unstrongef(r));
}

template <typename R, typename T>
constexpr bool in_range(T t) noexcept {
  constexpr auto T_min = numeric_limits<T>::min;
  constexpr auto R_min = numeric_limits<R>::min;

  if constexpr (cmp_less(T_min, R_min)) {
    T cmp(R_min);
    if (t < cmp) {
      return false;
    }
  }

  constexpr auto T_max = numeric_limits<T>::max;
  constexpr auto R_max = numeric_limits<R>::max;

  if constexpr (cmp_greater(T_max, R_max)) {
    T cmp(R_max);
    if (t > cmp) {
      return false;
    }
  }

  return true;
}
}  // namespace noexport

template <typename To, typename From>
To safe_cast(From from) {
  if (!noexport::in_range<To>(from)) {
    throw std::invalid_argument("cannot safe cast");
  }
  return To(from);
}

template <typename Dst, typename Src>
void safe_write(Src src, Dst& dst) {
  dst = safe_cast<Dst>(src);
}

}  // namespace svarg
