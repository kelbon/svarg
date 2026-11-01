// ОПЕРАЦИИ: файл операции - параметры, тела, ответы, result_t, extern инстанциации

#include "render_common.hpp"

namespace svarg::cxx {

std::string renderer::render_body_type(const ir::body& body, std::string_view spec,
                                       const json_pointer& jp) const {
  // без схемы (@any@) и без встроенного кодека (octet-stream, image/png...) - байты как есть
  if (body.schema.is_fundamental(ir::fundamental_e::any) &&
      body_format_of(body.contentType.str()) == body_format_e::raw)
    return "std::string";
  return render_schema_ref(body.schema, spec, jp).typeref;
}

rendered_with_name renderer::render_body(const ir::body& body, std::string_view spec,
                                         const json_pointer& jp) const {
  // throw_if_unsupported_content_type(body.contentType);
  std::string name = std::format("{}_body", make_cxx_ident(body.contentType, options));
  std::string result;
  result += std::format("struct {} {{\n", name);
  result += std::format("static constexpr bool is_required = {};\n", body.required);
  result += render_parse_tag(cxx_support::parse_tag_e::body);
  assert(is_lowered(body.contentType.str()));
  result += std::format("static constexpr std::string_view content_type = {};\n\n",
                        render_string(body.contentType));
  result += render_special_field(render_body_type(body, spec, jp));

  result += "};\n";

  return {std::move(name), std::move(result)};
}

std::string renderer::render_bodies(const std::set<ir::body>& bodies, std::string_view spec,
                                    const json_pointer& jp) const {
  std::string result;
  if (bodies.empty())
    return result;
  if (bodies.size() > 1) {
    std::vector<std::string> struct_names;
    for (auto& b : bodies) {
      auto [struct_name, str] = render_body(b, spec, jp);
      struct_names.push_back(std::move(struct_name));
      result += str;
      result += '\n';
    }
    auto names_rng = struct_names | std::views::join_with(',');
    std::string types = std::format("{}", std::string(names_rng.begin(), names_rng.end()));
    result += std::format("{}<std::monostate, {}> body;\n\n", variant_str, types);
    // боди уже сортированы по типу контента так, что сначала провериться более специализированный
    result +=
        "static constexpr decltype(auto) select_body_type(std::string_view content_type, auto&& vtor) {";
    for (auto&& [b, struct_name] : std::views::zip(bodies, struct_names))
      result += std::format(
          "if (svarg::match_content_type<{}>(content_type)) return vtor.template operator()<{}>();\n",
          render_string(b.contentType.str()), struct_name);
    result += std::format("return vtor.template operator()<void>();\n");
    result += "}\n\n";
  } else {
    // одно тело: без обёртки, свойства тела - статические члены владельца
    auto& body = *bodies.begin();
    assert(is_lowered(body.contentType.str()));
    result += std::format("static constexpr std::string_view body_content_type = {};\n",
                          render_string(body.contentType));
    result += std::format("static constexpr bool body_is_required = {};\n", body.required);
    std::string type = render_body_type(body, spec, jp);
    if (body.required)
      result += std::format("\n{} body;\n", type);
    else
      result += std::format("\n{}<{}> body;\n", optional_str, type);
  }

  return result;
}

// генерирует
// struct default_response|response_{status} {
//   int status = 0; // static constexpr если статус известен заранее
//   struct headers_t { ... }
//   headers_t headers; // если есть хедера
//   body_A, body_B... // если есть боди
//   body_t body;
// };
std::string_view renderer::stream_content_type_of(const ir::response_desc& rsp) const {
  if (rsp.bodies)
    for (const ir::body& b : *rsp.bodies)
      if (std::ranges::contains(options.stream_content_types, b.contentType.str()))
        return b.contentType.str();
  return {};
}

bool renderer::has_stream_responses(const ir::operation& op) const {
  if (op.defaultResponse && !stream_content_type_of(*op.defaultResponse).empty())
    return true;
  return op.responses && std::ranges::any_of(*op.responses, [&](const ir::response_desc& r) {
           return !stream_content_type_of(r).empty();
         });
}

rendered_with_name renderer::render_response(const ir::response_desc& rsp, std::string_view spec,
                                             const json_pointer& jp) const {
  std::string result;
  std::string struct_name =
      rsp.is_default() ? "default_response" : std::format("response_{}", rsp.status.to_string());

  result += std::format("struct {} {{\n\n", struct_name);

  result += render_parse_tag(cxx_support::parse_tag_e::response);

  if (!rsp.is_default()) {
    if (!rsp.is_range())
      // имя совпадает с полем, которое не генерируется когда есть чёткий статус
      result += std::format("static constexpr int status = {};\n\n", rsp.status.to_string());
    else
      result += std::format("static constexpr status_range_e status_range = status_range_e::{};\n\n",
                            rsp.status.to_string());
  }
  if (rsp.is_default() || rsp.is_range())
    result += "int status = 0;\n";
  if (rsp.headers && !rsp.headers->empty()) {
    std::vector<ir::operation_parameter> params;
    for (auto& h : *rsp.headers)
      params.push_back(header_as_parameter(h));
    result += "struct headers_t {\n";
    result += render_parameters(params, spec, jp);
    result += "};\n";
    result += "headers_t headers;\n";
  }
  // ответ с потоковым медиа типом - поток целиком, его тела (и непотоковые) не генерируются
  if (std::string_view stream_ct = stream_content_type_of(rsp); !stream_ct.empty()) {
    result += std::format(
        "// тело - поток: сервер отправляет ctx.stream_response(rsp.status, make_response_headers(rsp), "
        "body), клиент получает send(op, on_header, on_data_part, ...)\n"
        "static constexpr std::string_view stream_content_type = {};\n",
        render_string(stream_ct));
  } else if (rsp.bodies) {
    result += render_bodies(*rsp.bodies, spec, jp);
  }
  result += "};\n\n";
  return {std::move(struct_name), std::move(result)};
}

// все схемы, упомянутые в `x` (операции, её параметрах, телах, ответах) - для include
static ir::schema_refs_t all_schemas_from(const auto& x, bool allow_fundamental = false) {
  ir::schema_refs_t schemas;
  auto foo = [&](const ir::schema_ref& s) {
    if (allow_fundamental || !s.is_fundamental())
      schemas.insert(s);
  };
  ir::schema_ref_vtor collector(foo);
  collector.visit(x);
  return schemas;
}

// генерирует visit_path операции: литералы пути и параметры по индексам полей path_t
struct build_path_make_fn_visitor {
  std::string& out;
  // в порядке полей path_t, порядок в шаблоне пути может отличаться
  const std::vector<const ir::operation_parameter*>& params;
  const render_options& options;

  explicit build_path_make_fn_visitor(std::string& x, std::vector<const ir::operation_parameter*>& params,
                                      const render_options& options)
      : out(x), params(params), options(options) {
    out += "static constexpr void visit_path(auto&& vtor) {";
  }

  size_t index_of(std::string_view param_name) const {
    auto it = std::ranges::find(params, param_name, [](auto* p) -> std::string_view { return p->name; });
    if (it == params.end())
      throw render_error("path parameter `{}` is not declared in `parameters`", param_name);
    return std::distance(params.begin(), it);
  }

  void substr(std::string_view sv) {
    out += std::format("vtor.substr({});\n", render_string(sv));
  }
  void param(std::string_view param_name) {
    out += std::format("vtor.param(std::index_sequence<{}>{{}});\n", index_of(param_name));
  }
  void param(std::string_view param_name, std::string_view sep) {
    out += std::format("vtor.param(std::index_sequence<{}>{{}}, {});\n", index_of(param_name),
                       render_string(sep));
  }

  void pattern_end() {
    out += "}\n\n";
  }
  void error(std::string_view msg) {
    throw render_error("{}", msg);
  }

  static consteval bool is_done() {
    return false;
  }
};

// явные инстанциации функций (де)сериализации операции и её ответов. В заголовке операции - extern (не
// инстанцируются у пользователя), определения - в .cpp спеки
std::string renderer::render_instantiations(const ir::operation& op, bool is_extern) const {
  std::string opname = std::format("::{}::{}", make_namespace(op.spec, json_pointer("/paths")),
                                   make_cxx_ident(op.id, options));
  std::string_view ext = is_extern ? "extern " : "";
  std::string r;
  r += std::format(
      "{0}template ::hidi::http_request svarg::cxx_support::make_request<{1}>(const {1}&, "
      "const ::svarg::content_type_params&, std::string_view);\n",
      ext, opname);
  r += std::format(
      "{0}template ::svarg::error_code svarg::cxx_support::from_request<{1}>(const ::hidi::http_request&, "
      "{1}&, std::string_view);\n",
      ext, opname);
  if (op.servers) {
    // ответы операции с ответами потоком пользователь разбирает сам
    if (has_stream_responses(op))
      r += std::format(
          "{0}template {1}::result_t svarg::cxx_support::parse_response<{1}>(::hidi::http_response&);\n", ext,
          opname);
    else
      r += std::format(
          "{0}template dd::task<{1}::result_t> svarg::cxx_support::send_request<{1}>("
          "::svarg::cxx_support::any_client&, ::hidi::http_request, ::hidi::deadline_t);\n",
          ext, opname);
  }
  auto response = [&](std::string_view struct_name, const ir::response_desc& rsp) {
    std::string t = std::format("{}::{}", opname, struct_name);
    // ответ потоком сервер отправляет сам (ctx.stream_response)
    if (stream_content_type_of(rsp).empty())
      r += std::format(
          "{0}template ::hidi::http_response svarg::cxx_support::make_response<{1}>(const {1}&, "
          "const ::svarg::content_type_params&);\n",
          ext, t);
    r += std::format(
        "{0}template ::svarg::error_code svarg::cxx_support::from_response<{1}>(const "
        "::hidi::http_response&, "
        "{1}&);\n",
        ext, t);
  };
  if (op.defaultResponse)
    response("default_response", *op.defaultResponse);
  if (op.responses)
    for (auto& rsp : *op.responses)
      response(std::format("response_{}", rsp.status.to_string()), rsp);
  return r;
}

render_result renderer::render_operation(const ir::operation& op, bool is_webhook) const {
  std::string result = render_file_begin();

  // обращения все из root, так как обращаемся из /paths в /components/schemas
  json_pointer paths("/paths");

  {  // includes
    auto used_schemas = dealias(all_schemas_from(op));
    for (auto& s : used_schemas)
      result += std::format("#include \"{}\"\n", make_file_path(s, options));
    result += '\n';
  }
  std::vector<const ir::operation_parameter*> path_params;
  std::vector<const ir::operation_parameter*> query_params;
  std::vector<const ir::operation_parameter*> header_params;
  std::vector<const ir::operation_parameter*> cookie_params;

  if (op.parameters) {
    for (auto& p : *op.parameters) {
      switch (p.location) {
        case parameter_location::path:
          path_params.push_back(&p);
          break;
        case parameter_location::query:
          query_params.push_back(&p);
          break;
        case parameter_location::header:
          header_params.push_back(&p);
          break;
        case parameter_location::cookie:
          cookie_params.push_back(&p);
          break;
      }
    }
  }

  result += std::format("namespace {} {{\n\n", make_namespace(op.spec, paths));

  // id
  // то, что нужно определить после операции
  std::string after_op;
  result += std::format("struct {} {{\n", make_cxx_ident(op.id, options));

  // parse tag
  result += render_parse_tag(cxx_support::parse_tag_e::operation);

  // spec
  result += std::format("static constexpr inline std::string_view spec = {};\n\n", render_string(op.spec));

  // method

  // uppered так как все методы в регистре методов должны быть upper (GET/POST а в опенапи файле пишутся
  // маленькими буквами). Игнорируем что какой-то кастомный метод зачем быть GeT
  result += std::format("static constexpr inline std::string_view method = {};\n",
                        render_string(uppered(op.method)));
  std::string path = deescape_path(op.path);
  // parameters
  if (op.has_path()) {
    // static constexpr void visit_path(auto&& vtor)
    visit_templated_path_parts(path, build_path_make_fn_visitor{result, path_params, options});
    result += std::format("static constexpr std::string_view path_expr = {};\n\n", render_string(path));
  }
  auto deref = std::views::transform([](auto* p) -> auto& { return *p; });
  if (!path_params.empty()) {
    if (!op.has_path())
      throw render_error("operation have path parameters while not having path");
    for (const ir::operation_parameter& p : path_params | deref) {
      if (!path.contains("{" + p.name + "}")) {
        throw render_error(
            "operation `{}` contains parameter `{}` which is not present in path expression `{}`", op.id,
            p.name, path);
      }
    }
    result += "struct path_t {\n";
    result += render_parameters(path_params | deref, op.spec, paths);
    result += "};\n";
  }
  if (!query_params.empty()) {
    result += "struct query_t {\n";
    result += render_parameters(query_params | deref, op.spec, paths);
    result += "};\n";
  }
  if (!header_params.empty()) {
    result += "struct headers_t {\n";
    result += render_parameters(header_params | deref, op.spec, paths);
    result += "};\n";
  }
  if (!cookie_params.empty()) {
    result += "struct cookies_t {\n";
    result += render_parameters(cookie_params | deref, op.spec, paths);
    result += "};\n";
  }
  result += '\n';
  if (!path_params.empty())
    result += "path_t path;\n";
  if (!query_params.empty())
    result += "query_t query;\n";
  if (!header_params.empty())
    result += "headers_t headers;\n";
  if (!cookie_params.empty())
    result += "cookies_t cookies;\n";

  // bodies

  if (op.bodies)
    result += render_bodies(*op.bodies, op.spec, paths);

  // responses/ defaultResponse

  // респонсов может быть не указано вообще - например в вебхуке для парсинга приходящих операций, а не
  // отправки
  if (op.defaultResponse)
    result += render_response(*op.defaultResponse, op.spec, paths).render;

  {
    // имена структур ответов в порядке op.responses
    std::vector<std::string> struct_names;
    if (op.responses) {
      for (auto& r : *op.responses) {
        assert(!r.is_default());
        auto [struct_name, rendered] = render_response(r, op.spec, paths);
        result += rendered;
        struct_names.push_back(std::move(struct_name));
      }
    }
    static const std::remove_cvref_t<decltype(*op.responses)> no_responses;
    const auto& responses = op.responses ? *op.responses : no_responses;
    auto statuses = [&](bool ranges) {
      return std::views::zip(struct_names, responses) |
             std::views::filter([ranges](auto&& x) { return std::get<1>(x).is_range() == ranges; });
    };

    result += "static constexpr decltype(auto) visit_response_status(int status, auto&& vtor) {\n";
    // сначала не ренжи (2ХХ)
    for (auto&& [struct_name, r] : statuses(/*ranges=*/false)) {
      result += std::format("if (status == {}) return vtor.template operator()<{}>();\n",
                            r.status.to_string(), struct_name);
    }
    // затам ренжи, чтобы более специализированный был проверен раньше
    for (auto&& [struct_name, r] : statuses(/*ranges=*/true)) {
      result += std::format(
          "if (svarg::match_status_range<status_range_e::{}>(status)) return vtor.template "
          "operator()<{}>();\n",
          r.status.to_string(), struct_name);
    }
    if (op.defaultResponse)
      result += "return vtor.template operator()<default_response>();\n";
    else
      result += "return vtor.template operator()<void>();\n";
    result += "}\n\n";

    // (имя структуры, суффикс get_if_)
    std::vector<std::pair<std::string, std::string>> results;
    for (auto&& [struct_name, r] : std::views::zip(struct_names, responses))
      results.emplace_back(struct_name, r.status.to_string());
    if (op.defaultResponse)
      results.emplace_back("default_response", "default");

    // результат клиента - описанные в спецификации ответы: единственный - сам ответ, несколько - вариант.
    // Статус не из спецификации попадает в default ответ, а без него клиент бросает unexpected_response
    if (results.size() == 1) {
      result += std::format("using result_t = {};\n", results.front().first);
    } else if (!results.empty()) {
      // определяется после операции: внутри неё clang считает ответы с полями по умолчанию
      // (вложенные классы с инициализаторами) не default constructible, и variant тоже
      result += "struct result_t;\n";
      std::string& result = after_op;
      result += std::format("struct {}::result_t {{\n", make_cxx_ident(op.id, options));
      auto names_rng = results | std::views::keys | std::views::join_with(',');
      result +=
          std::format("\n{}<{}> data;\n\n", variant_str, std::string(names_rng.begin(), names_rng.end()));
      for (auto& [struct_name, suffix] : results) {
        result +=
            std::format("const {0}* get_if_{1}() const noexcept {{ return std::get_if<{0}>(&data); }}\n",
                        struct_name, suffix);
        result += std::format("{0}* get_if_{1}() noexcept {{ return std::get_if<{0}>(&data); }}\n",
                              struct_name, suffix);
      }
      result += "};\n";
    } else {
      result += "struct result_t {};\n";  // ответов нет, любой статус неожиданный
    }
  }

  result += "};\n\n";  // struct
  result += after_op;
  result += "}\n";  // namespace
  result += "\n// определены в .cpp спеки\n";
  result += render_instantiations(op, /*is_extern=*/true);
  result += operation_file_end;
  return {make_file_path(op, options, is_webhook), std::move(result)};
}

}  // namespace svarg::cxx
