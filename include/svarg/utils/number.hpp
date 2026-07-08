#pragma once

#include <cstdint>
#include <variant>
#include <utility>

#include "svarg/errors.hpp"

namespace svarg {

struct number {
  std::variant<int64_t, uint64_t, double> data;

  number() noexcept : data(int64_t(0)) {
  }

  number(int64_t i) noexcept : data(i) {
  }
  number(uint64_t i) noexcept : data(i) {
  }
  number(double i) noexcept : data(i) {
  }

  number(const number&) = default;
  number(number&&) = default;
  number& operator=(const number&) = default;
  number& operator=(number&&) = default;

  ~number() = default;

  number& operator=(int64_t i) noexcept {
    data = i;
    return *this;
  }
  number& operator=(uint64_t i) noexcept {
    data = i;
    return *this;
  }
  number& operator=(double i) noexcept {
    data = i;
    return *this;
  }

  double get_as_double() const noexcept {
    return std::visit([](auto v) { return double(v); }, data);
  }

  int64_t get_as_int64() const {
    switch (data.index()) {
      case 0:
        return std::get<int64_t>(data);
      case 1: {
        uint64_t v = std::get<uint64_t>(data);
        if (!std::in_range<int64_t>(v))
          throw parse_error("value `{}` is not in range of int64_t", v);
        return v;
      }
      case 2:
        return int64_t(std::get<double>(data));
    }
    metel::unreachable();
  }

  uint64_t get_as_uint64() const {
    switch (data.index()) {
      case 0: {
        auto v = std::get<int64_t>(data);
        if (v < 0)
          throw parse_error("value `{}` is not in range of uint64_t", v);
        return uint64_t(v);
      }
      case 1:
        return std::get<uint64_t>(data);
      case 2: {
        auto v = std::get<double>(data);
        if (v < 0)
          throw parse_error("value `{}` is not in range of uint64_t", v);
        return uint64_t(v);
      }
    }
    metel::unreachable();
  }
};

}  // namespace svarg
