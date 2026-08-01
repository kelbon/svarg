#include "svarg/backend/cxx/support/h2clients_pool.hpp"

#include <hidi/asio/awaiters.hpp>

namespace svarg::cxx_support {

make_ssl_fn default_make_ssl() {
  return [](const dest_and_src&) { return hidi::make_ssl_context_for_client({}); };
}

h2clients_pool::h2clients_pool(duration_t idle_client_lifetime, h2client_options options, make_ssl_fn fn)
    : io(),
      m_options(std::move(options)),
      m_gc_coro(),
      m_gc_timer(hidi::asio_timer(io)),
      m_make_ssl_fn(std::move(fn)) {
  set_max_idle_period(idle_client_lifetime);
}

dd::job h2clients_pool::gc_loop() {
  hidi::io_error_code ec;
  on_scope_exit {
    m_gc_coro.handle = nullptr;
  };
  for (;;) {
    auto it = m_clients.begin();
    // co_await в этом цикле делать нельзя - инвалидация итераторов
    while (it != m_clients.end()) {
      if (it->second.use_count() == 1) {
        // клиент удалится как только его перестанут использовать (shared_ptr)
        if (!it->second->used && !it->second->client.connecting()) {
          it = m_clients.erase(it);
          continue;
        }
        it->second->used = false;
      }
      ++it;
    }

    // делим период на 2, так как неактивный клиент удаляется за 2 итерации
    co_await hidi::net.sleep(m_gc_timer, m_gc_interval / 2, ec);
    if (ec)
      co_return;
  }
}

void h2clients_pool::stop() {
  m_gc_timer.cancel();
  m_clients.clear();
  // мы в одном потоке, так что gc_loop всегда стоит на sleep
  assert(m_gc_coro.handle == nullptr);
}

std::shared_ptr<h2client> h2clients_pool::get_connection_to(dest_and_src addr) {
  resolve_addr(addr);
  if (auto it = m_clients.find(addr); it != m_clients.end()) {
    // нашли клиента в кеше, возможно он ещё в процессе соединения
    it->second->used = true;
    // ассоциированный shared_ptr
    return std::shared_ptr<h2client>(it->second, &it->second->client);
  }
  // создаём нового клиента ссылающегося на 'io'
  hidi::client_ssl_context_ptr ssl = m_make_ssl_fn ? m_make_ssl_fn(addr) : nullptr;
  // если ssl == nullptr -> http
  std::shared_ptr desc =
      std::make_shared<client_desc>(addr, m_options, hidi::make_asio_tls_io_context(io, ssl));
  m_clients.try_emplace(addr, desc);
  return std::shared_ptr<h2client>(desc, &desc->client);
}

void h2clients_pool::resolve_addr(dest_and_src& addr) {
  auto& e = addr.dest;
  if (e.ipaddr())
    return;
  auto* fqdn = e.fqdn();
  auto it = dns_cache.find(*fqdn);
  if (it != dns_cache.end()) {
    e = hidi::endpoint(it->second, e.get_port());
    return;
  }
  boost::asio::ip::tcp::resolver r(io);
  assert(e.get_port() != 0);
  auto res = r.resolve(*fqdn, std::format("{}", e.get_port()));
  auto [ep_it, _] = dns_cache.try_emplace(*fqdn, res.begin()->endpoint().address());
  e = hidi::endpoint(ep_it->second, e.get_port());
}

}  // namespace svarg::cxx_support
