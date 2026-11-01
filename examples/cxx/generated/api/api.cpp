#include "api/server.hpp"
#include "api/client.hpp"

namespace api {

::svarg::cxx_support::router server::make_routes() {
  ::svarg::cxx_support::router r;
  r.add_route(hidi::http_method_e::GET, "/ping", 0);
  return r;
}

dd::task<hidi::http_response> server::handle_request(hidi::http_request req, hidi::request_context ctx) {
  std::optional id = routes_.find(req.method, svarg::path_and_query(req.path).first);
  if (!id)
    return on_unknown_operation(std::move(req), std::move(ctx));
  switch (*id) {
    case 0: {
      ping op;
      svarg::error_code ec = svarg::cxx_support::from_request(req, op, "");
      if (ec)
        return on_bad_request(std::move(req), std::move(ec), std::move(ctx));
      return handle(std::move(op), std::move(req), std::move(ctx));
    }
    default:
      break;
  }
  return on_user_route(*id, std::move(req), std::move(ctx));
}

}  // namespace api

namespace api {

dd::task<ping::result_t> client::send_ping(const ping& op, hidi::deadline_t deadline,
                                           const svarg::content_type_params& ctp) {
  return svarg::cxx_support::send_request<ping>(
      transport, svarg::cxx_support::make_request(op, ctp, base_path), deadline);
}

}  // namespace api

#include "api/operations/ping.hpp"

template ::hidi::http_request svarg::cxx_support::make_request<::api::ping>(
    const ::api::ping&, const ::svarg::content_type_params&, std::string_view);
template ::svarg::error_code svarg::cxx_support::from_request<::api::ping>(const ::hidi::http_request&,
                                                                           ::api::ping&, std::string_view);
template dd::task<::api::ping::result_t> svarg::cxx_support::send_request<::api::ping>(
    ::svarg::cxx_support::any_client&, ::hidi::http_request, ::hidi::deadline_t);
template ::hidi::http_response svarg::cxx_support::make_response<::api::ping::response_200>(
    const ::api::ping::response_200&, const ::svarg::content_type_params&);
template ::svarg::error_code svarg::cxx_support::from_response<::api::ping::response_200>(
    const ::hidi::http_response&, ::api::ping::response_200&);
