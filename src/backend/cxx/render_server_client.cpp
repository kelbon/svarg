// СЕРВЕР И КЛИЕНТ: server.hpp, client.hpp и их определения в .cpp спеки

#include "render_common.hpp"

namespace svarg::cxx {

// путь из url сервера без завершающего '/', переменные заменяются значениями по умолчанию
// "https://api.example.com/v1/" -> "/v1", "/" -> ""
static std::string server_base_path(const server_object& s) {
  std::string url;
  std::string_view tmpl = s.url;
  while (!tmpl.empty()) {
    size_t open = tmpl.find('{');
    url += tmpl.substr(0, open);
    if (open == tmpl.npos)
      break;
    size_t close = tmpl.find('}', open);
    if (close == tmpl.npos)
      throw render_error("invalid server url `{}`", s.url);
    std::string_view name = tmpl.substr(open + 1, close - open - 1);
    auto var = s.variables ? s.variables->find(name) : decltype(s.variables->find(name)){};
    if (!s.variables || var == s.variables->end())
      throw render_error("server url `{}` uses undefined variable `{}`", s.url, name);
    url += var->second.default_;
    tmpl.remove_prefix(close + 1);
  }
  std::string_view path = url;
  if (!is_relative_url(url))
    path = parse_absolute_url(url).path;
  path = path.substr(0, path.find_first_of("?#"));
  while (path.ends_with('/'))
    path.remove_suffix(1);
  if (!path.empty() && !path.starts_with('/'))
    throw render_error("server url `{}` has path not starting with '/'", s.url);
  return std::string(path);
}

namespace {

// операция по полному пути (base path сервера + путь операции)
struct route {
  const ir::operation* op;
  std::string base_path;
  std::string path;  // включая base_path
};

}  // namespace

// TODO теоретически пути делятся на "точное соответствие" - хеш мапа и для динамических и для статических,
// без аргументов пути и может префиксное дерево для остального. Но существуют неоднозначности

// возвращает сортированных набор маршрутов такой, что по нему можно идти сверху вниз при матче path
// и первый сматченный будет наиболее специализированным
// возвращает мапу метод -> маршруты (сначала не шаблонные пути, затем шаблонные сортированные по приоритету)
static string_map<std::vector<route>> order_routes_for_path_matching(std::vector<route> routes) {
  auto is_not_templated = [](const route& r) { return !is_template_path(r.path); };
  // TODO такую неоднозначность (см. ниже) можно решать по правилу точное совпадение выигрывает. В данном
  // случае v1 это точное совпадение с пришедшей строкой, а значит выигрывает она?

  // приоритет важен для предсказуемости выбора в неоднозначных ситуациях
  // /v1/{a} и /{version}/x могут сматчить одну и ту же строку "/v1/x"
  // хотя среди них нет более специализированного
  // для разрешения этой ситуации используется приоритет,
  // в опенапи приоритет выше если операция написана выше в спеке

  auto by_priority = [](const route& a, const route& b) {
    // меньше приоритет - выше в списке. У всех должен быть назначен приоритет, так как вебхуки не должны
    // быть присоединены к серверу
    return a.op->priority.value() < b.op->priority.value();
  };
  auto by_specialization = [by_priority](const route& a, const route& b) {
    auto order = compare_paths_specialized(a.path, b.path);
    if (order != std::strong_ordering::equivalent)
      // в начале самые приоритетные
      return order == std::strong_ordering::greater;
    return by_priority(a, b);
  };

  // метод -> маршруты
  string_map<std::vector<route>> ordered;
  for (auto& r : routes)
    ordered[r.op->method].push_back(std::move(r));
  // сортируем внутри вектора по специализации и приоритету
  for (auto& [method, rs] : ordered) {
    // сначала не шаблонизированные пути
    auto templated = std::ranges::partition(rs, is_not_templated);
    std::ranges::stable_sort(rs.begin(), templated.begin(), by_priority);
    std::ranges::stable_sort(templated, by_specialization);
  }
  return ordered;
}

std::string renderer::render_server(const std::vector<const ir::operation*>& operations,
                                    std::string_view spec, std::string& source) const {
  assert(!operations.empty());

  std::string result = render_file_begin();
  for (auto* op : operations)
    result += std::format("#include \"{}\"\n", make_file_path(*op, options, /*is_webhook=*/false));
  result += std::format("namespace {} {{\n\n", make_namespace(spec, {}));

  // TODO x-name для сервера поддержать ?
  result += "struct server : hidi::h2server {\nusing hidi::h2server::h2server;\n\n";

  auto make_handler_method_name = [&](const ir::operation& op) {
    return std::format("do_{}", make_cxx_ident(op.id, options));
  };

  // виртуальные функции операций

  for (auto* op : operations) {
    assert(op);
    // возвращает http_response чтобы пользователь мог гибче назначить всякие параметры content type
    // или сделать что-то неподдерживаемое библиотекой
    result += std::format(
        "virtual dd::task<hidi::http_response> {}({}, hidi::http_request, hidi::request_context) = "
        "0;\n\n",
        make_handler_method_name(*op), make_cxx_ident(op->id, options));
  }

  result +=
      "// путь и метод не подошли ни к одной операции\n"
      "virtual dd::task<hidi::http_response> on_unknown_operation(hidi::http_request, "
      "hidi::request_context) {\n"
      "co_return hidi::http_response{.status = 404};\n}\n\n"
      "// операция найдена, но запрос не соответствует ей (параметры, тело...)\n"
      "virtual dd::task<hidi::http_response> on_bad_request(hidi::http_request, svarg::error_code, "
      "hidi::request_context) {\n"
      "co_return hidi::http_response{.status = 400};\n}\n\n";

  // определение в .cpp спеки, чтобы разбор всех операций не инстанцировался у пользователя
  result +=
      "dd::task<hidi::http_response> handle_request(hidi::http_request req, hidi::request_context ctx) "
      "override;\n";
  result += "};\n\n}\n";  // end struct, namespace

  std::string& impl = source;
  impl += std::format("namespace {} {{\n\n", make_namespace(spec, {}));
  impl +=
      "dd::task<hidi::http_response> server::handle_request(hidi::http_request req, hidi::request_context "
      "ctx) "
      "{\n"
      "std::string_view path = svarg::path_and_query(req.path).first;\n";

  // операция доступна по base path каждого своего сервера
  std::vector<route> routes;
  for (auto* op : operations) {
    std::set<std::string> base_paths;
    for (auto& srv : *op->servers)
      base_paths.insert(server_base_path(srv));
    for (auto& bp : base_paths)
      routes.push_back(route{.op = op, .base_path = bp, .path = bp + deescape_path(op->path)});
  }

  impl += "switch(req.method) {\n";
  for (auto& [method, rs] : order_routes_for_path_matching(std::move(routes))) {
    impl += std::format("case hidi::http_method_e::{}:\n", method);
    for (auto& r : rs) {
      impl += std::format(
          "if (svarg::match_path_pattern<{}>(path)) {{\n"
          "{} op;\n"
          "svarg::error_code ec = svarg::cxx_support::from_request(req, op, {});\n"
          "if (ec) return on_bad_request(std::move(req), std::move(ec), std::move(ctx));\n"
          "return {}(std::move(op), std::move(req), std::move(ctx));\n"
          "}}\n",
          render_string(r.path), make_cxx_ident(r.op->id, options), render_string(r.base_path),
          make_handler_method_name(*r.op));
    }
    impl += "break;\n";
  }
  impl += "default: break;\n}\n";  // end switch
  impl += "return on_unknown_operation(std::move(req), std::move(ctx));\n";
  impl += "}\n\n}\n";  // end fn, namespace
  return result;
}

std::string renderer::render_client(const std::vector<const ir::operation*>& operations,
                                    std::string_view spec, std::string& source) const {
  assert(!operations.empty());
  std::string res;

  res += render_file_begin();
  for (auto* op : operations)
    res += std::format("#include \"{}\"\n", make_file_path(*op, options, /*is_webhook=*/false));

  res += std::format("namespace {} {{\n\n", make_namespace(spec, {}));
  source += std::format("namespace {} {{\n\n", make_namespace(spec, {}));

  // клиент отправляет на один адрес, поэтому операции группируются по набору серверов
  std::map<std::set<server_object>, std::vector<const ir::operation*>> groups;
  for (auto* op : operations)
    groups[*op->servers].push_back(op);
  // группа с наибольшим числом операций (обычно глобальные `servers`) - просто `client`
  auto main_group = std::ranges::max_element(groups, std::less{}, [](auto& g) { return g.second.size(); });

  for (auto it = groups.begin(); it != groups.end(); ++it) {
    auto& [servers, ops] = *it;
    const server_object& first = *servers.begin();
    res += "// servers:\n";
    for (auto& srv : servers)
      res += std::format("//   {}\n", srv.url);
    std::string name = "client";
    if (it != main_group) {
      // без схемы: client_upload_example_com_v2
      std::string_view url = first.url;
      if (auto pos = url.find("://"); pos != url.npos)
        url.remove_prefix(pos + 3);
      name = std::format("client_{}", make_cxx_ident(url, options));
    }
    res += std::format("struct {} {{\n", name);
    res += "svarg::cxx_support::any_client transport;\n";
    res += "// путь из url сервера, добавляется перед путём каждой операции\n";
    res += std::format("std::string base_path = {};\n\n", render_string(server_base_path(first)));

    for (auto* op : ops) {
      auto opname = make_cxx_ident(op->id, options);
      res += std::format(
          "dd::task<{0}::result_t> send_{0}(const {0}& op, hidi::deadline_t deadline, "
          "const svarg::content_type_params& ctp = {{}});\n",
          opname);
      // определение в .cpp спеки
      source += std::format(
          "dd::task<{0}::result_t> {1}::send_{0}(const {0}& op, hidi::deadline_t deadline, "
          "const svarg::content_type_params& ctp) {{\n"
          "return svarg::cxx_support::send_request<{0}>(transport, "
          "svarg::cxx_support::make_request(op, ctp, base_path), deadline);\n"
          "}}\n\n",
          opname, name);
    }
    res += "};\n\n";
  }

  res += "}\n";  // end namespace
  source += "}\n";
  return res;
}

}  // namespace svarg::cxx
