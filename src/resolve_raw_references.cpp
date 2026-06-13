
#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>

#include "svarg/openapi.hpp"
#include "svarg/utils/parse_uri.hpp"

#include <http2/http2_client.hpp>

namespace svarg {

[[nodiscard]] static bool is_net_ref(std::string_view s) {
  return s.starts_with("https://") || s.starts_with("http://");
}

// преобразует путь(который также может быть url) к нормальному виду без ./ и прочей фигни относительной
// при этом оставляя https:// и не убирая //
static std::string fixup_path(fs::path p) {
  std::string gs = p.generic_string();
  if (is_net_ref(gs)) {
    parsed_uri u = parse_uri(gs);
    std::string newpath = fs::path(u.path).lexically_normal().generic_string();
    u.path = newpath;
    return u.to_string();
  } else {
    return fs::canonical(p).lexically_normal().generic_string();
  }
}

static dd::task<void> do_load_refered_object(http2::http2_client& client, parsed_uri uri, auto success,
                                             auto fail) try {
  http2::http_request req{.path = std::string(uri.path), .method = http2::http_method_e::GET};
  req.authority = uri.host;
  http2::http_response rsp =
      co_await client.send_request(req, http2::deadline_after(std::chrono::seconds(5)));
  // редирект не обрабатывается
  if (rsp.status < 200 || rsp.status >= 300) {
    fail(std::format("invalid rsp with status={}", rsp.status).c_str());
    co_return;
  }
  success(rsp.body_strview());
} catch (std::exception& e) {
  fail(e.what());
}

// загруженное body иди исключение
// никаких кешей, плевать на перф здесь +- совсем, просто на случай если кому-то очень надо
std::string do_get_request_to(std::string_view uri) {
  std::string fixed = fixup_path(uri);
  uri = fixed;
  parsed_uri u = parse_uri(uri);
  std::optional<http2::http2_client> client;
  http2::http2_client_options opts{.allow_requests_before_server_settings = true};
  if (uri.starts_with("http://")) {
    client.emplace(http2::endpoint(std::string(u.host), u.get_port()), opts);
  } else if (uri.starts_with("https://")) {
    client.emplace(http2::endpoint(std::string(u.host), u.get_port()), opts,
                   [](boost::asio::io_context& ctx) { return http2::default_tls_transport_factory(ctx); });
  } else
    throw parse_error("invalid $ref, network ref must start with http:// or https://, ref: `{}`", uri);

  std::optional<std::string> body;
  std::optional<std::string> err;
  auto suc = [&](std::string_view res) { body = res; };
  auto fail = [&](const char* what) { err = what; };
  do_load_refered_object(*client, u, suc, fail).start_and_detach();
  while (!body && !err)
    client->ioctx().poll();
  if (err)
    throw parse_error("cannot load uri(`{}`), err: {}", uri, *err);
  assert(body);
  return std::move(*body);
}

// если в ref нет # (разделителя на путь до файла и json поинтер) это означает include json/yaml по URI
// делает блокирующий GET запрос по URL с/без SSL (url должно начинаться с http:// или https://)
// а затем парсит ответ как json
static boost::json::value request_refered_object(std::string_view uri) {
  std::string body = do_get_request_to(uri);
  if (uri.ends_with(".yaml") || uri.ends_with(".yml"))
    return boost::json::parse(yaml2json(body));
  else  // пробуем как json в любом случае
    return boost::json::parse(body);
  return {};
}

struct load_raw_ref_return {
  boost::json::value value;
  // путь до файла с value
  fs::path path;
};

// `url` предполагается без #, просто ссылка на файл
// `curfile` должен быть относительным путём или URL (или также может быть URL, если загрузили файл до этого)
static load_raw_ref_return load_raw_refered_object(std::string_view url, fs::path curfile,
                                                   const svarg_options& options, jsons_cache* cache) {
  curfile = fixup_path(std::move(curfile));
  // вроде если не обрабатывать путь может получится слишком длинным и в итоге
  // сломается всё. А если обрабатывать - https:// -> https:/ может выйти боком
  assert(!url.contains('#'));
  // TODO здесь путь обрабывается и https:// может превратиться в https:/

  // абсолютные ссылки в сеть
  if (is_net_ref(url)) {
    if (!options.resolve_url_refs) {
      throw parse_error(
          "loading external refs disabled due to options - resolve_url_refs = false by default. ref: `{}`, "
          "path: `{}`",
          url, curfile.generic_string());
    }
    return load_raw_ref_return(request_refered_object(url), url);
  }
  // до этого загрузили из сети, но сейчас это уже внутри загруженного файла локальная ссылка
  if (is_net_ref(curfile.generic_string())) {
    // странно что мы уже загружали и опять, но обработаем на всякий
    if (!options.resolve_url_refs) {
      throw parse_error(
          "loading external refs disabled due to options - resolve_url_refs = false by default. ref: `{}`, "
          "path: `{}`",
          url, curfile.generic_string());
    }
    fs::path path = curfile.parent_path() / url;
    return load_raw_ref_return(request_refered_object(path.generic_string()), path.generic_string());
  }
  boost::json::value result;
  fs::path path = curfile.parent_path() / url;
  if (cache) {
    result = cache->load(path);
  } else {
    pass_as_json(path, [&](std::string_view data) { result = boost::json::parse(data); });
  }
  return load_raw_ref_return(std::move(result), std::move(path));
}

struct raw_ref {
  boost::json::value* ref = nullptr;
  boost::json::value* parent = nullptr;
  std::string value;
};

// собирает все $ref в значении которых нет # и возвращает массив объектов/массивов которые их содержат (а не
// сам объект ссылки)
static void collect_raw_refs(boost::json::value* v, std::vector<raw_ref>& refs) {
  if (v->is_array()) {
    for (auto&& elem : v->as_array())
      collect_raw_refs(&elem, refs);
  } else if (v->is_object()) {
    auto& obj = v->as_object();
    for (auto&& [key, value] : v->as_object()) {
      // поля x- могут содержать любую дичь, бессмысленно ссылки там резолвить
      // например здесь вставляют .php / C# код в примеры
      // https://github.com/Redocly/openapi-starter/blob/main/openapi/paths/echo.yaml
      // с другой стороны есть x- хедеры, где тоже могут быть ссылки, их нельзя исключать
      // так что дурачки с расширениями идут лесом
      // if (key.starts_with("x-"))
      //  continue;
      if (key == "$ref") {
        if (!value.is_string())
          throw parse_error("$ref value must be string, got: `{}`", e2str(value.kind()));
        auto&& str = value.get_string();
        if (auto pos = str.find('#'); pos != str.npos)
          continue;
        refs.push_back(raw_ref{.ref = &obj["$ref"], .parent = v, .value = std::string(str)});
      } else {
        collect_raw_refs(&value, refs);
      }
    }
  }
}

// если depth дошло до 0, то больше погружаться нельзя
// ссылки разрешаются относительно curfile
static void include_raw_references(boost::json::value& v, fs::path curfile, size_t depth,
                                   const svarg_options& options, jsons_cache* cache) {
  std::vector<raw_ref> refs;
  collect_raw_refs(&v, refs);
  if (refs.empty())
    return;
  if (depth == 0) {
    std::string links;
    for (auto& r : refs) {
      links += r.value;
      links.push_back(';');
    }
    if (options.ref_depth_is_a_fail)
      throw parse_error("refs resolution max depth reached, yet unresolved links `{}`", links);
    else
      return;
  }

  for (auto& [ref, parent, value] : refs) {
    auto [replace, replace_path] = load_raw_refered_object(value, curfile, options, cache);
    include_raw_references(replace, replace_path, depth - 1, options, cache);
    assert(ref && parent);
    if (!replace.is_object())
      *ref = std::move(replace);
    else {
      if (parent->is_object()) {
        auto& parentobj = parent->as_object();
        // merge, не заменяем поля которые должны перекрыть то что в $ref
        // не запрещая при этом рекурсивные ссылки
        parentobj.erase("$ref");
        for (auto&& [rk, rv] : replace.as_object()) {
          if (!parentobj.contains(rk) || rk == "$ref")
            parentobj[rk] = std::move(rv);
        }
      } else {
        *ref = std::move(replace);
      }
    }
  }
}

boost::json::value resolve_raw_references(boost::json::value v, fs::path specfile,
                                          const svarg_options& options, jsons_cache* cache) {
  include_raw_references(v, std::move(specfile), options.ref_max_depth, options, cache);
  return v;
}

}  // namespace svarg
