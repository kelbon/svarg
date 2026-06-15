#pragma once

#include "svarg/ir/ir_object.hpp"
#include "svarg/serialize.hpp"

namespace svarg {

template <>
struct discriminator_t<ir::fundamental_e> {
  static constexpr std::string_view value = "fundamental";
};

template <>
struct serialize<ir::fundamental_e> {
  boost::json::value operator()(const ir::fundamental_e& e) const noexcept {
    return boost::json::value{e2str(e)};
  }
};

template <>
struct serialize<ir::status_range_e> {
  boost::json::value operator()(const ir::status_range_e& e) const noexcept {
    return boost::json::value{e2str(e)};
  }
};

}  // namespace svarg
