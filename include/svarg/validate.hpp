#pragma once

#include "svarg/errors.hpp"

namespace svarg {

template <typename T>
struct validate {
  [[nodiscard]] error_code operator()(const T&) {
    // всё валидируется при парсинге агрегата, так что если какое-то поле имеет доп ограничения они проверятся
    return {};
  }
};

// вызывается после парсинга перед validate, например чтобы нормализовать результат
template <typename T>
struct postparse {
  [[nodiscard]] error_code operator()(T&) {
    return {};
  }
};

}  // namespace svarg
