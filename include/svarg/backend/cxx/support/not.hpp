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
  static error_code operator()(const boost::json::value& j, cxx_support::not_a<T>&) {
    T value;
    error_code ec = try_from_json(j, value);
    if (ec)
      return {};
    return parse_error("when parsing not_a<{}>. Unexpected success of parsing", type_name<T>());
  }
};

template <typename T>
struct to_json_value<cxx_support::not_a<T>> {
  // сериализация not запрещена, так как он не может быть представлением типа. Только требованием
  static boost::json::value operator()(const cxx_support::not_a<T>&) = delete;
};

}  // namespace svarg
