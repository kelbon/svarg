// NRF (реестр сервисов ядра 5G) по реальной спецификации 3GPP TS 29.510: сервер с реестром в памяти и
// клиент, который регистрирует сервисы, шлёт heartbeat, ищет их, подписывается на изменения и снимает с
// регистрации. Всё, что ниже, - обычный код поверх сгенерированных типов: разбор, проверка схем,
// маршрутизация и сериализация уже сделаны svarg

#include <print>
#include <map>

#include "TS29510_Nnrf_NFManagement/client.hpp"
#include "TS29510_Nnrf_NFManagement/server.hpp"
#include "TS29510_Nnrf_NFManagement/webhooks/postOnNFStatusEvent.hpp"

namespace nrf = TS29510_Nnrf_NFManagement;
using namespace nrf::components::schemas;                 // NFProfile, NFType, NFStatus...
using namespace TS29571_CommonData::components::schemas;  // общие типы 3GPP: ProblemDetails, PatchItem...
using svarg::cxx_support::get_additional_properties;
using svarg::cxx_support::make_response;

static constexpr std::string_view api_root = "/nnrf-nfm/v1";

static ProblemDetails problem(int status, std::string cause, std::string detail) {
  ProblemDetails p;
  p.status = status;
  p.cause = std::move(cause);
  p.detail = std::move(detail);
  return p;
}

// СЕРВЕР

struct nrf_server : nrf::server {
  using nrf::server::server;

  std::map<std::string, NFProfile> profiles;  // nfInstanceId -> профиль
  std::map<std::string, SubscriptionData> subscriptions;
  int next_subscription = 1;

  // регистрация (PUT): 201 и Location для нового NF, 200 для повторной
  dd::task<hidi::http_response> handle(nrf::RegisterNFInstance op, hidi::http_request,
                                       hidi::request_context) override {
    const std::string& id = op.path.nfInstanceID.get();
    op.body.heartBeatTimer.emplace().get() = 10;  // NRF назначает период heartbeat
    auto [it, created] = profiles.insert_or_assign(id, op.body);
    notify(NotificationEventType::NF_REGISTERED, it->second);
    if (!created) {
      nrf::RegisterNFInstance::response_200 rsp;
      rsp.body = it->second;
      co_return make_response(rsp);
    }
    nrf::RegisterNFInstance::response_201 rsp;
    rsp.headers.location = std::format("{}/nf-instances/{}", api_root, id);
    rsp.body = it->second;
    co_return make_response(rsp);
  }

  // heartbeat и частичное изменение (PATCH, JSON Patch). Пример поддерживает только замену статуса
  dd::task<hidi::http_response> handle(nrf::UpdateNFInstance op, hidi::http_request,
                                       hidi::request_context) override {
    auto it = profiles.find(op.path.nfInstanceID.get());
    if (it == profiles.end())
      co_return make_response(nrf::UpdateNFInstance::response_404{
          .body = problem(404, "RESOURCE_NOT_FOUND", "unknown NF instance")});
    for (const PatchItem& item : op.body.get()) {
      if (item.op == PatchOperation::replace && item.path == "/nfStatus" && item.value &&
          item.value->is_string())
        it->second.nfStatus = item.value->get_string();
    }
    co_return make_response(nrf::UpdateNFInstance::response_204{});
  }

  dd::task<hidi::http_response> handle(nrf::GetNFInstance op, hidi::http_request,
                                       hidi::request_context) override {
    auto it = profiles.find(op.path.nfInstanceID.get());
    if (it == profiles.end())
      co_return make_response(nrf::GetNFInstance::response_404{
          .body = problem(404, "RESOURCE_NOT_FOUND", "unknown NF instance")});
    nrf::GetNFInstance::response_200 rsp;
    rsp.body = it->second;
    co_return make_response(rsp);
  }

  // список (application/3gppHal+json): ссылки на NF, по желанию только заданного типа
  dd::task<hidi::http_response> handle(nrf::GetNFInstances op, hidi::http_request,
                                       hidi::request_context) override {
    LinksValueSchema_::oneOf::_0 items;
    for (auto& [id, profile] : profiles) {
      if (op.query.nf_type && profile.nfType != *op.query.nf_type)
        continue;
      items.get().push_back(Link{.href = std::format("{}/nf-instances/{}", api_root, id)});
    }
    nrf::GetNFInstances::response_200 rsp;
    rsp.body.totalItemCount = items.get().size();
    LinksValueSchema links;
    links.get().emplace<LinksValueSchema_::oneOf::_0>(std::move(items));
    get_additional_properties(rsp.body._links.emplace())["items"] = std::move(links);
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(nrf::DeregisterNFInstance op, hidi::http_request,
                                       hidi::request_context) override {
    auto it = profiles.find(op.path.nfInstanceID.get());
    if (it == profiles.end())
      co_return make_response(nrf::DeregisterNFInstance::response_404{
          .body = problem(404, "RESOURCE_NOT_FOUND", "unknown NF instance")});
    notify(NotificationEventType::NF_DEREGISTERED, it->second);
    profiles.erase(it);
    co_return make_response(nrf::DeregisterNFInstance::response_204{});
  }

  // подписка на изменения: 201, Location и выданный subscriptionId
  dd::task<hidi::http_response> handle(nrf::CreateSubscription op, hidi::http_request,
                                       hidi::request_context) override {
    std::string id = std::to_string(next_subscription++);
    op.body.subscriptionId.emplace().get() = id;
    subscriptions[id] = op.body;
    nrf::CreateSubscription::response_201 rsp;
    rsp.headers.location = std::format("{}/subscriptions/{}", api_root, id);
    rsp.body = std::move(op.body);
    co_return make_response(rsp);
  }

  dd::task<hidi::http_response> handle(nrf::RemoveSubscription op, hidi::http_request,
                                       hidi::request_context) override {
    subscriptions.erase(op.path.subscriptionID.get());
    co_return make_response(nrf::RemoveSubscription::response_204{});
  }

  // остальное спеки в примере не реализовано
  dd::task<hidi::http_response> handle(nrf::UpdateSubscription, hidi::http_request,
                                       hidi::request_context) override {
    co_return not_implemented();
  }
  dd::task<hidi::http_response> handle(nrf::OptionsNFInstances, hidi::http_request,
                                       hidi::request_context) override {
    co_return not_implemented();
  }
  dd::task<hidi::http_response> handle(nrf::RegisterSharedData, hidi::http_request,
                                       hidi::request_context) override {
    co_return not_implemented();
  }
  dd::task<hidi::http_response> handle(nrf::UpdateSharedData, hidi::http_request,
                                       hidi::request_context) override {
    co_return not_implemented();
  }
  dd::task<hidi::http_response> handle(nrf::GetSharedData, hidi::http_request,
                                       hidi::request_context) override {
    co_return not_implemented();
  }
  dd::task<hidi::http_response> handle(nrf::DeleteSharedData, hidi::http_request,
                                       hidi::request_context) override {
    co_return not_implemented();
  }

  // свой маршрут рядом с операциями спеки (см. main): /health
  dd::task<hidi::http_response> on_user_route(size_t, hidi::http_request, hidi::request_context) override {
    hidi::http_response rsp{.status = 200};
    std::string_view ok = "ok";
    rsp.body.assign(ok.begin(), ok.end());
    co_return rsp;
  }

  static hidi::http_response not_implemented() {
    hidi::http_response rsp{.status = 501};
    return rsp;
  }

  // уведомление подписчикам: callback спеки (postOnNFStatusEvent) - только сгенерированная структура,
  // отправить её - дело приложения (свой any_client на nfStatusNotificationUri). Здесь - просто печать
  void notify(std::string_view event, const NFProfile& nf) {
    for (auto& [id, sub] : subscriptions) {
      // условие подписки - oneOf из 17 видов, здесь проверяется только по типу NF
      if (auto* cond = sub.subscrCond.get().get_if<NfTypeCond>(); cond && cond->nfType != nf.nfType)
        continue;
      nrf::postOnNFStatusEvent n;
      n.body.emplace().event = event;
      n.body->nfInstanceUri = std::format("{}/nf-instances/{}", api_root, nf.nfInstanceId.get());
      hidi::http_request req = svarg::cxx_support::make_request(n);
      std::println("  -> {}: {}", sub.nfStatusNotificationUri, req.body.strview());
    }
  }
};

// КЛИЕНТ

static NFProfile make_profile(std::string_view id, std::string_view type, std::string_view name) {
  NFProfile p;
  p.nfInstanceId.get() = id;
  p.nfType = type;
  p.nfStatus = NFStatus::REGISTERED;
  p.nfInstanceName = name;
  // спека требует хотя бы один из fqdn, ipv4Addresses, ipv6Addresses (anyOf в NFProfile): без него
  // профиль не пройдёт проверку схемы уже при отправке
  p.fqdn.emplace().get() = std::format("{}.5gc.example", name);
  return p;
}

static dd::task<void> scenario(nrf::client& cli, hidi::h2client& h2) {
  auto never = hidi::deadline_t::never();
  std::string amf = "4947a69a-f61b-4bc1-b9da-47c9c5d14b64";
  std::string smf = "8ac83c4a-4c5c-4b11-9e6f-c4d5e2bc2b30";

  // подписка: уведомлять о регистрациях AMF
  nrf::CreateSubscription sub;
  sub.body.nfStatusNotificationUri = "http://amf-watcher/notify";
  sub.body.subscrCond.get().emplace<NfTypeCond>(NfTypeCond{.nfType = NFType::AMF});
  nrf::CreateSubscription::result_t subscribed = co_await cli.send(sub, never);
  if (auto* created = subscribed.get_if_201())
    std::println("subscribed: {}", created->headers.location);

  // регистрация двух NF
  for (auto [id, type, name] :
       {std::tuple{amf, NFType::AMF, "amf-1"}, std::tuple{smf, NFType::SMF, "smf-1"}}) {
    nrf::RegisterNFInstance reg;
    reg.path.nfInstanceID.get() = id;
    reg.body = make_profile(id, type, name);
    nrf::RegisterNFInstance::result_t r = co_await cli.send(reg, never);
    if (auto* created = r.get_if_201())
      std::println("registered {}: {}, heartbeat {}s", name, created->headers.location,
                   created->body.heartBeatTimer->get());
  }

  // heartbeat: JSON Patch со сменой статуса
  nrf::UpdateNFInstance hb;
  hb.path.nfInstanceID.get() = amf;
  hb.body.get().push_back(PatchItem{
      .path = "/nfStatus", .op = PatchOperation::replace, .value = boost::json::string("SUSPENDED")});
  nrf::UpdateNFInstance::result_t updated = co_await cli.send(hb, never);
  std::println("heartbeat: {}", updated.get_if_204() ? "204" : "?");

  nrf::GetNFInstance get;
  get.path.nfInstanceID.get() = amf;
  nrf::GetNFInstance::result_t amf_profile = co_await cli.send(get, never);
  if (auto* ok = amf_profile.get_if_200())
    std::println("amf status: {}", ok->body.nfStatus.value);

  // поиск по типу: ссылки HAL
  nrf::GetNFInstances list;
  list.query.nf_type = NFType::SMF;
  nrf::GetNFInstances::result_t found = co_await cli.send(list, never);
  if (auto* ok = found.get_if_200()) {
    auto& items = get_additional_properties(*ok->body._links).at("items").get();
    for (const Link& link : items.get_if<LinksValueSchema_::oneOf::_0>()->get())
      std::println("found SMF: {}", *link.href);
  }

  // снятие с регистрации и ошибка в application/problem+json
  nrf::DeregisterNFInstance dereg;
  dereg.path.nfInstanceID.get() = smf;
  (void)co_await cli.send(dereg, never);
  get.path.nfInstanceID.get() = smf;
  nrf::GetNFInstance::result_t gone = co_await cli.send(get, never);
  if (auto* not_found = gone.get_if_404())
    std::println("smf: {} {}", not_found->body.status.value(), *not_found->body.cause);

  // свой маршрут - обычный запрос hidi
  hidi::http_response health = co_await h2.send_request(hidi::http_request{.path = "/health"}, never);
  std::println("health: {} {}", health.status, health.body_strview());
}

int main() {
  nrf_server server(hidi::server_ssl_context_ptr(nullptr));  // http, без tls
  server.routes().add_route(hidi::http_method_e::GET, "/health", nrf_server::user_route_begin);
  hidi::internet_address addr =
      server.listen(hidi::server_endpoint{.addr = {boost::asio::ip::address_v4::loopback(), 0}});

  hidi::h2client h2{hidi::endpoint(addr)};
  // транспорт можно обернуть в свои middleware (ретраи, авторизация...), см. any_client.hpp
  nrf::client cli{svarg::cxx_support::client_ref(h2)};

  bool done = false;
  auto run = [&]() -> dd::task<void> {
    try {
      co_await scenario(cli, h2);
    } catch (std::exception& e) {
      std::println("error: {}", e.what());
    }
    done = true;
  };
  run().start_and_detach();
  // сервер и клиент в одном потоке
  while (!done) {
    server.ioctx().poll();
    h2.ioctx().poll();
  }
  return 0;
}
