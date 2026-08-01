#pragma once

#include "common_types.hpp"

#include "api/operations/ping.hpp"
namespace api {
struct client {
  svarg::cxx_support::h2clients_pool& pool;
  dd::task<svarg::cxx_support::operation_result<ping>> send_ping(ping op, std::string host,
                                                                 hidi::deadline_t deadline,
                                                                 svarg::content_type_params ctp = {}) const {
    return svarg::cxx_support::send_request<ping>(*pool.get_connection_to({hidi::endpoint(host), {}}),
                                                  svarg::cxx_support::make_request(op, ctp), deadline);
  }
};

}  // namespace api
