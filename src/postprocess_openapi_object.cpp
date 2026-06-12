#include "svarg/openapi.hpp"

#include "svarg/utils/strcmp.hpp"

namespace svarg {

namespace detail {

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-14
// "If a response header is defined with the name "Content-Type", it SHALL be ignored."
static void remove_content_type_header(optional<string_map<reference_or<header_object>>>& headers) {
  if (!headers)
    return;
  std::erase_if(*headers, [](auto const& kv) { return is_equal_ci(kv.first, "Content-Type"); });
}

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-9
// "If `in` is `header` and the `name` field is "Accept", "Content-Type" or "Authorization",
//  the parameter definition SHALL be ignored."
static void remove_ignored_header_params(optional<array<reference_or<parameter_object>>>& params) {
  if (!params)
    return;
  static constexpr std::string_view ignored[] = {"Accept", "Content-Type", "Authorization"};
  std::erase_if(*params, [](reference_or<parameter_object> const& rop) {
    auto* p = std::get_if<parameter_object>(&rop);
    if (!p)
      return false;
    if (p->in != parameter_location::header)
      return false;
    // по спеке не написано явно как сравнивать хедера, но в HTTP/2 хедера case insensitive
    // поэтому логично не допускать conTeNt-tYpe и тд хедера здесь
    for (auto name : ignored)
      if (is_equal_ci(p->name, name))
        return true;
    return false;
  });
}

// https://spec.openapis.org/oas/v3.1.0#fixed-fields-12
// "Content-Type is described separately and SHALL be ignored in this section."
static void remove_content_type_from_encoding_headers(optional<string_map<encoding_object>>& encoding) {
  if (!encoding)
    return;
  for (auto& [_, enc] : *encoding)
    if (enc.headers) {
      // TODO case insentisive
      enc.headers->erase("Content-Type");
      enc.headers->erase("content-type");
    }
}

static void postprocess_media_type_object(media_type_object& mt) {
  remove_content_type_from_encoding_headers(mt.encoding);
}

static void postprocess_response_object(response_object& r) {
  remove_content_type_header(r.headers);
  if (r.content)
    for (auto& [_, mt] : *r.content)
      postprocess_media_type_object(mt);
}

static void postprocess_operation_object(operation_object& op) {
  remove_ignored_header_params(op.parameters);
  if (op.requestBody)
    if (auto* rb = std::get_if<request_body_object>(&*op.requestBody))
      for (auto& [_, mt] : rb->content)
        postprocess_media_type_object(mt);
  if (op.responses) {
    if (op.responses->default_)
      if (auto* r = std::get_if<response_object>(&*op.responses->default_))
        postprocess_response_object(*r);
    for (auto& [_, ror] : op.responses->_responses)
      if (auto* r = std::get_if<response_object>(&ror))
        postprocess_response_object(*r);
  }
}

static void postprocess_path_item_object(reference_or<path_item_object>& pir) {
  auto* pi = std::get_if<path_item_object>(&pir);
  if (!pi)
    // TODO вообще-то там могут рядом со ссылкой лежать всякие данные, которые требуют обработки.
    // Пока игнорируется
    return;
  remove_ignored_header_params(pi->parameters);
  auto visit_op = [](optional<operation_object>& op) {
    if (op)
      postprocess_operation_object(*op);
  };
  visit_op(pi->get);
  visit_op(pi->put);
  visit_op(pi->post);
  visit_op(pi->delete_);
  visit_op(pi->options);
  visit_op(pi->head);
  visit_op(pi->patch);
  visit_op(pi->trace);
}

}  // namespace detail

// вызывается после парсинга и валидации openapi_object дабы убрать все вещи, которые по спецификации
// требуется игнорировать / добавить некоторые вещи, которые должны быть по умолчанию для упрощения дальнейшей
// обработки
void postprocess_openapi_object(openapi_object& obj) {
  // https://spec.openapis.org/oas/v3.1.0#fixed-fields
  // "If the `servers` property is not provided, or is an empty array, the default value
  //  would be a Server Object with a url value of `/`."
  if (!obj.servers || obj.servers->empty())
    obj.servers = array<server_object>{{.url = "/"}};

  if (obj.paths)
    for (auto& [_, pi] : *obj.paths)
      detail::postprocess_path_item_object(pi);

  if (obj.webhooks)
    for (auto& [_, pi] : *obj.webhooks)
      detail::postprocess_path_item_object(pi);

  if (obj.components) {
    if (obj.components->responses)
      for (auto& [_, ror] : *obj.components->responses)
        if (auto* r = std::get_if<response_object>(&ror))
          detail::postprocess_response_object(*r);

    if (obj.components->headers)
      for (auto& [_, hor] : *obj.components->headers)
        if (auto* h = std::get_if<header_object>(&hor))
          if (h->content)
            for (auto& [_, mt] : *h->content)
              detail::postprocess_media_type_object(mt);

    if (obj.components->pathItems)
      for (auto& [_, pi] : *obj.components->pathItems)
        detail::postprocess_path_item_object(pi);
  }
}

}  // namespace svarg
