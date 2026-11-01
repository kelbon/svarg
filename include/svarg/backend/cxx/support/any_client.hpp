#pragma once

#include <anyany/anyany.hpp>
#include <anyany/anyany_macro.hpp>

#include <hidi/h2client.hpp>

// транспорт сгенерированных клиентов: всё, что умеет отправить HTTP запрос. Интерфейс как у
// hidi::h2client::send_request с колбэками: `on_header` на каждый хедер ответа, `on_data_part` на каждую
// часть тела, результат - статус ответа или < 0 при ошибке (hidi::reqerr_e). Ответ целиком собирают
// сгенерированные клиенты поверх этого (send_buffered).
//
// Middleware (ретраи, хедеры, авторизация, логирование) - это тоже any_client, оборачивающий другой:
//
//   struct with_headers {
//     any_client next;
//     http_headers_t headers;
//     dd::task<int> send_request(on_header_fn_ptr h, on_data_part_fn_ptr d, http_request req, deadline_t dl)
//     {
//       req.headers.append_range(headers);
//       return next.send_request(h, d, std::move(req), dl);  // без co_await: не создаёт лишний фрейм
//     }
//   };
//
// Ретраи должны укладываться в дедлайн и не повторять неидемпотентные запросы без явного разрешения. Если
// часть ответа уже отдана в колбэк, повторять нельзя

namespace svarg::cxx_support {

// `on_header` и `on_data_part` могут быть nullptr - хедеры / тело игнорируются
anyany_method2_n(
    send_request_m, send_request,
    (&self, hidi::on_header_fn_ptr on_header, hidi::on_data_part_fn_ptr on_data_part, hidi::http_request req,
     hidi::deadline_t deadline) requires(self.send_request(on_header, on_data_part, std::move(req), deadline))
        ->dd::task<int>);

using any_client = aa::any_with<aa::move, send_request_m>;

// hidi::h2client не перемещаемый, поэтому хранится по ссылке. Клиент должен пережить все запросы
struct client_ref {
  hidi::h2client* client;

  explicit client_ref(hidi::h2client& c) noexcept : client(&c) {
  }

  dd::task<int> send_request(hidi::on_header_fn_ptr on_header, hidi::on_data_part_fn_ptr on_data_part,
                             hidi::http_request req, hidi::deadline_t deadline) {
    return client->send_request(on_header, on_data_part, std::move(req), deadline);
  }
};

// статус < 0 от транспорта - исключение, как в hidi::h2client::send_request(req, deadline)
[[noreturn]] void throw_transport_error(int status);

// ответ целиком: хедеры и тело собираются в http_response, ошибки транспорта - исключения
[[nodiscard]] dd::task<hidi::http_response> send_buffered(any_client&, hidi::http_request, hidi::deadline_t);

}  // namespace svarg::cxx_support
