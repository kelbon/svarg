#include "svarg/backend/cxx/support/any_client.hpp"

#include <hidi/errors.hpp>
#include <hidi/h2errors.hpp>

namespace svarg::cxx_support {

void throw_transport_error(int status) {
  assert(status < 0);
  using enum hidi::reqerr_e::values_e;
  switch (hidi::reqerr_e::values_e(status)) {
    case TIMEOUT:
      throw hidi::timeout_exception{};
    case NETWORK_ERR:
      throw hidi::network_exception{""};
    case PROTOCOL_ERR:
      throw hidi::protocol_error(hidi::errc_e::PROTOCOL_ERROR);
    case CANCELLED:
      throw std::runtime_error("HTTP client: request was canceled");
    case SERVER_CANCELLED_REQUEST:
      throw std::runtime_error("HTTP client: request was canceled by server");
    default:
      throw std::runtime_error("HTTP client unknown error happens");
  }
}

dd::task<hidi::http_response> send_buffered(any_client& client, hidi::http_request req,
                                            hidi::deadline_t deadline) {
  hidi::http_response rsp;
  auto on_header = [&](std::string_view name, std::string_view value) {
    rsp.headers.emplace_back(std::string(name), std::string(value));
  };
  auto on_data_part = [&](std::span<const hidi::byte_t> bytes, bool /*last_part*/) {
    rsp.body.insert(rsp.body.end(), bytes.begin(), bytes.end());
  };
  rsp.status = co_await client.send_request(&on_header, &on_data_part, std::move(req), deadline);
  if (rsp.status < 0)
    throw_transport_error(rsp.status);
  co_return rsp;
}

}  // namespace svarg::cxx_support
