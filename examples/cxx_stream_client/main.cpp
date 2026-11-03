// Клиент с ответом потоком: скачивает файлы у сервера из примера cxx_stream_server (его нужно запустить).
// Тело ответа 200 (поток по спеке) приходит в колбэк по мере прихода, ответ 404 svarg собирает и разбирает
// как обычно

#include <print>

#include "api/client.hpp"

static dd::task<void> download(api::client& cli, std::string name) {
  api::getFile op;
  op.path.name = name;
  auto on_part = [](int /*status*/, std::string_view /*content_type*/, std::span<const hidi::byte_t> part,
                    bool last) {
    std::println("part: '{}'{}", std::string_view((const char*)part.data(), part.size()),
                 last ? " (last)" : "");
  };
  api::getFile::result_t r = co_await cli.send(op, &on_part, hidi::deadline_after(std::chrono::seconds(5)));
  if (auto* ok = r.get_if_200())
    std::println("{}: done, x-file-size {}", name, ok->headers.x_file_size);
  else if (auto* not_found = r.get_if_404())
    std::println("{}: 404 {}", name, not_found->body.message);
}

int main() {
  hidi::h2client h2{hidi::endpoint(boost::asio::ip::address_v4::loopback(), 8080)};
  api::client cli{svarg::cxx_support::client_ref(&h2)};

  auto run = [&]() -> dd::task<void> {
    try {
      co_await download(cli, "hello.txt");
      co_await download(cli, "missing.txt");
    } catch (std::exception& e) {
      std::println("error: {}", e.what());
    }
    h2.ioctx().stop();
  };
  run().start_and_detach();
  h2.ioctx().run();
}
