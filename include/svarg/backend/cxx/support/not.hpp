#pragma once

#include "svarg/from_json_value.hpp"
#include "svarg/to_json_value.hpp"

namespace svarg::cxx_support {

// по сути просто тег для парсинга. Сериализация not невозможна,
// т.к. этот тип не может быть представлением, только требованием
template <typename T>
struct not_a {};

}  // namespace svarg::cxx_support

namespace svarg {

template <typename T>
struct from_json_value<cxx_support::not_a<T>> {
  static error_code operator()(const boost::json::value& j, cxx_support::not_a<T>& v) {
    ;  // TODO;
  }
};

template <typename T>
struct to_json_value<cxx_support::not_a<T>> {
  static boost::json::value operator()(const cxx_support::not_a<T>& v) {
    ;  // TODO;
  }
};

}  // namespace svarg
