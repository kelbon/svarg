#pragma once

#include <anyany/anyany.hpp>
#include <anyany/anyany_macro.hpp>

#include <hidi/h2client.hpp>

// транспорт сгенерированных клиентов: всё, что умеет отправить HTTP запрос.
// Middleware (ретраи, хедеры, авторизация, логирование) - это тоже any_client, оборачивающий другой:
//
//   struct with_headers {
//     any_client next;
//     http_headers_t headers;
//     dd::task<http_response> send_request(http_request req, deadline_t d) {
//       req.headers.append_range(headers);
//       return next.send_request(std::move(req), d);  // без co_await: не создаёт лишний фрейм
//     }
//   };
//
// Ретраи должны укладываться в дедлайн и не повторять неидемпотентные запросы без явного разрешения

namespace svarg::cxx_support {

// ошибки (соединения, таймаут) сообщаются исключениями, как в hidi::h2client::send_request
anyany_method2_n(send_request_m, send_request,
                 (&self, hidi::http_request req,
                  hidi::deadline_t deadline) requires(self.send_request(std::move(req), deadline))
                     ->dd::task<hidi::http_response>);

using any_client = aa::any_with<aa::move, send_request_m>;

// hidi::h2client не перемещаемый, поэтому хранится по ссылке. Клиент должен пережить все запросы
struct client_ref {
  hidi::h2client* client;

  explicit client_ref(hidi::h2client& c) noexcept : client(&c) {
  }

  dd::task<hidi::http_response> send_request(hidi::http_request req, hidi::deadline_t deadline) {
    return client->send_request(std::move(req), deadline);
  }
};

}  // namespace svarg::cxx_support
