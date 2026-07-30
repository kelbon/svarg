#pragma once

#include "svarg/backend/cxx/support/http_base.hpp"
#include "svarg/utils/combined_hash.hpp"

#include <kelcoro/job.hpp>

#include <metel/fixed_array.hpp>

namespace svarg::cxx_support {

struct dest_and_src {
  hidi::endpoint dest;
  std::optional<hidi::internet_address> src;

  bool operator==(const dest_and_src&) const = default;
};

}  // namespace svarg::cxx_support

namespace boost {

template <typename... Types>
struct hash<::std::variant<Types...>> {
  size_t operator()(const ::std::variant<Types...>& v) const {
    return std::visit([]<typename T>(const T& v) { return boost::hash<T>{}(v); }, v);
  }
};

template <>
struct hash<::hidi::ip_addr> {
  size_t operator()(const ::hidi::ip_addr& e) const noexcept {
    if (e.is_unspecified())
      return 0;
    if (e.is_v4())
      return ::svarg::hash_combined(e.to_v4().to_bytes());
    else {
      assert(e.is_v6());
      return ::svarg::hash_combined(e.to_v6().to_bytes());
    }
  }
};

template <>
struct hash<::hidi::internet_address> {
  size_t operator()(const ::hidi::internet_address& e) const noexcept {
    return ::svarg::hash_combined(e.address(), e.port());
  }
};

template <>
struct hash<::hidi::endpoint> {
  size_t operator()(const ::hidi::endpoint& e) const noexcept {
    return ::svarg::hash_combined(e.addr, e.port);
  }
};

}  // namespace boost

namespace std {

template <>
struct hash<::hidi::endpoint> {
  size_t operator()(const ::hidi::endpoint& e) const noexcept {
    return ::svarg::hash_combined(e.addr, e.port);
  }
};

template <>
struct hash<::svarg::cxx_support::dest_and_src> {
  size_t operator()(const ::svarg::cxx_support::dest_and_src& v) const noexcept {
    return ::svarg::hash_combined(v.dest, v.src);
  }
};

}  // namespace std

namespace svarg::cxx_support {

// создаёт ssl контекст для клиента
using make_ssl_fn = hidi::fn<hidi::client_ssl_context_ptr(const dest_and_src&)>;

make_ssl_fn default_make_ssl();

// должно быть использовано только в одном потоке (как и все выданные клиенты)
// TODO хм, кешировать по резолвленным адресам...
struct h2clients_pool {
 private:
  // TODO dns cache endpoint -> internet_address + clearCache
  // чтобы невозможно было чтобы один и тот же хост зарезолвился в два разных адреса из-за изменения dns по
  // ходу
  // и днс зависит исключительно от хостнейма
  struct client_desc {
    hidi::h2client client;
    bool used = true;

    client_desc(dest_and_src addrs, hidi::h2client_options options, hidi::any_io_context io)
        : client(addrs.dest, std::move(options), std::move(io)) {
      client.set_local(addrs.src);
    }
  };

  boost::asio::io_context io;
  // список клиентов - как хоть раз успешно соединившихся, так и соединяющихся
  // fqdn -> first resolved addr
  std::unordered_map<std::string, hidi::ip_addr> dns_cache;
  // все dest адреса зарезолвленны
  std::unordered_map<dest_and_src, std::shared_ptr<client_desc>> m_clients;
  dd::job m_gc_coro;
  hidi::any_timer m_gc_timer;
  hidi::duration_t m_gc_interval;
  hidi::h2client_options m_options;
  make_ssl_fn m_make_ssl_fn;

 public:
  // опции применяются к каждому создаваемому клиенту
  // если !ssl.has_value() (или возвращается nullptr) клиенты создаются http, а не https
  explicit h2clients_pool(hidi::duration_t client_lifetime_after_last_use = std::chrono::seconds(30),
                          hidi::h2client_options = {}, make_ssl_fn ssl = default_make_ssl());

  h2clients_pool(h2clients_pool&&) = delete;
  void operator=(h2clients_pool&&) = delete;

  ~h2clients_pool() {
    stop();
  }

  void run() {
    assert(!m_gc_coro.handle);
    m_gc_coro = gc_loop();
    io.run();
  }

  void stop();

  // работает даже с другого потока (ставит задачу остановится в очередь)
  void request_stop() {
    boost::asio::post(io, [this] { stop(); });
  }
  // назначает время, спустя которое неиспользуемый клиент будет удалён из кеша
  void set_max_idle_period(hidi::duration_t client_lifetime_after_last_use) {
    m_gc_interval = client_lifetime_after_last_use;
  }

  // Возвращает клиент с запрошенным адресом назначения (не обязательно уже соединённый)
  // pre: addr.dest.port != 0
  dd::task<std::shared_ptr<hidi::h2client>> get_connection_to(dest_and_src addr, hidi::deadline_t deadline);

 private:
  void resolve_addr(dest_and_src&);
  dd::job gc_loop();
};

struct h2clients_pool_mt {
 private:
  metel::fixed_array<h2clients_pool> pools;

 public:
  explicit h2clients_pool_mt(size_t pools_count,
                             hidi::duration_t client_lifetime_after_last_use = std::chrono::seconds(30),
                             hidi::h2client_options options = {}, make_ssl_fn ssl = default_make_ssl())
      : pools(pools_count, std::in_place, client_lifetime_after_last_use, std::move(options),
              std::move(ssl)) {
  }

  h2clients_pool_mt(h2clients_pool_mt&&) = delete;
  void operator=(h2clients_pool_mt&&) = delete;

  ~h2clients_pool_mt() = default;

  h2clients_pool& get(size_t n) noexcept {
    return pools[n];
  }

  void run(size_t n) {
    pools[n].run();
  }

  void request_stop() {
    for (auto& p : pools)
      p.request_stop();
  }
};

}  // namespace svarg::cxx_support
