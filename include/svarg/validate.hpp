#pragma once

#include "svarg/fundamental_types.hpp"

namespace svarg {

template <typename T>
struct validate {
  void operator()(const T&) {
    // всё валидируется при парсинге агрегата, так что если какое-то поле имеет доп ограничения они проверятся
  }
};

// вызывается после парсинга перед validate, например чтобы нормализовать результат
template <typename T>
struct postparse {
  void operator()(T&) {
  }
};

}  // namespace svarg
