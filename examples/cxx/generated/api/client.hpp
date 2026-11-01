#pragma once

#include "common_types.hpp"

#include "api/operations/ping.hpp"
namespace api {

// servers:
//   /
struct client {
  svarg::cxx_support::any_client transport;
  // путь из url сервера, добавляется перед путём каждой операции
  std::string base_path = "";

  dd::task<ping::result_t> send_ping(const ping& op, hidi::deadline_t deadline,
                                     const svarg::content_type_params& ctp = {});
};

}  // namespace api
