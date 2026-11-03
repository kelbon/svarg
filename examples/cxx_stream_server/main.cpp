// Сервер с ответом потоком: GET /files/{name} отдаёт файл (здесь - строку в памяти) по кускам, 404 - обычный
// JSON ответ. Клиент к нему - пример cxx_stream_client

#include <map>

#include "api/server.hpp"

using svarg::cxx_support::make_response;
using svarg::cxx_support::make_response_headers;

static const std::map<std::string, std::string, std::less<>> files = {
    {"hello.txt", "hello from svarg, streamed in parts\n"},
};

// тело потоком: каждый co_yield - отдельный кусок, клиент получает их по мере отправки
static hidi::streaming_body_t parts(std::string_view data) {
  for (size_t i = 0; i < data.size(); i += 8) {
    std::string_view part = data.substr(i, 8);
    co_yield std::span((const hidi::byte_t*)part.data(), part.size());
  }
}

struct stream_server : api::server {
  using api::server::server;

  dd::task<hidi::http_response> handle(api::getFile op, hidi::http_request,
                                       hidi::request_context ctx) override {
    auto file = files.find(op.path.name);
    if (file == files.end())
      co_return make_response(api::getFile::response_404{.body = {.message = "no file " + op.path.name}});
    // у потокового ответа нет тела, только хедеры (и статус)
    api::getFile::response_200 rsp;
    rsp.headers.x_file_size = file->second.size();
    // make_response_headers - хедеры из спеки и content-type
    co_return ctx.stream_response(rsp.status, make_response_headers(rsp), parts(file->second));
  }
};

int main() {
  stream_server server(hidi::server_ssl_context_ptr(nullptr));  // http, без tls
  server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 8080}});
  server.run();
}
