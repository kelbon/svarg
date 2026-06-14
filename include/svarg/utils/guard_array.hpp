#pragma once

#include <zal/zal.hpp>
#include <cassert>

namespace svarg {

// вставляет пустой массив в опшнл и возвращает гвард, который снова опустошает опшнл, если массив оказался
// пустым нужно для корректного поведения и не создавания пустых массивов, если пользователь дал поле с
// массивом, но оно пустое
template <typename Optional>
[[nodiscard]] auto guard_array(Optional& o) {
  assert(!o);

  o.emplace();
  return zal::scope_exit{[&o] {
    if (o->empty())
      o.reset();
  }};
}

}  // namespace svarg
