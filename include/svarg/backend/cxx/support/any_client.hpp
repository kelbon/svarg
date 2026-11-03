#pragma once

#include <anyany/anyany.hpp>
#include <anyany/anyany_macro.hpp>

#include <hidi/h2client.hpp>

namespace svarg::cxx_support {

// `on_header` и `on_data_part` могут быть nullptr - хедеры / тело игнорируются
anyany_method2_n(send_request_m, send_request,
                 (&self, hidi::client_request_context& ctx, hidi::on_header_fn_ptr on_header,
                  hidi::on_data_part_fn_ptr on_data_part, hidi::http_request req,
                  hidi::deadline_t deadline) requires(self.send_request(ctx, on_header, on_data_part,
                                                                        std::move(req), deadline))
                     ->dd::task<int>);

using any_client = aa::any_with<aa::move, send_request_m>;

struct client_ref {
  hidi::h2client* client;

  client_ref(hidi::h2client* c) noexcept : client(c) {
    assert(c);
  }

  dd::task<int> send_request(hidi::client_request_context& ctx, hidi::on_header_fn_ptr on_header,
                             hidi::on_data_part_fn_ptr on_data_part, hidi::http_request req,
                             hidi::deadline_t deadline) {
    return client->send_request(ctx, on_header, on_data_part, std::move(req), deadline);
  }
};

// статус < 0 от транспорта - ошибка
[[noreturn]] void throw_transport_error(int status);

// ответ целиком: хедеры и тело собираются в http_response, ошибки транспорта - исключения
[[nodiscard]] dd::task<hidi::http_response> send_buffered(any_client&, hidi::http_request, hidi::deadline_t);

}  // namespace svarg::cxx_support
