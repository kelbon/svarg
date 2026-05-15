#pragma once

#include "svarg/fundamental_types.hpp"

namespace svarg {

template <typename T>
struct validate {
  void operator()(const T&) {
    // всё валидируется при парсинге агрегата, так что если какое-то поле имеет доп ограничения они проверятся
  }
};

}  // namespace svarg
