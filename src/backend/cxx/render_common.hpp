#pragma once

// общее для всех частей C++ бэкенда (render_*.cpp): имена runtime-шаблонов, идентификаторы, строки,
// пути файлов и имена схем, рендер структур параметров

#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/namespace_strings.hpp"
#include "svarg/backend/cxx/render.hpp"
#include "svarg/x_meta_keys.hpp"
#include "svarg/errors.hpp"
#include "svarg/ir/ir_visitor.hpp"

#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/utils/parse_url.hpp"

#include <limits>

namespace svarg::cxx {

using cxx_support::special_field_name;

// используется для опциональных полей
inline constexpr std::string_view optional_str = "std::optional";
inline constexpr std::string_view variant_str = "std::variant";
// используется также как optional, но когда поля рекурсивные (т.е. нельзя сделать optional)
inline constexpr std::string_view box_str = "::metel::box";
// название шаблона куда подставлять аргументы allOf
inline constexpr std::string_view allof_str = "::svarg::cxx_support::allof";
inline constexpr std::string_view anyof_str = "::svarg::cxx_support::anyof";
inline constexpr std::string_view oneof_str = "::svarg::cxx_support::oneof";
inline constexpr std::string_view array_str = "::svarg::cxx_support::arrayof";
// куда вставлять аргумент 'not' из openapi
inline constexpr std::string_view not_str = "::svarg::cxx_support::not_a";
// для схемы без имени. Она будет в отдельном уникальном неймспейсе с этим именем
inline constexpr std::string_view inline_schema_name = "_schema";
// добавляется в конце каждой схемы
inline constexpr std::string_view schema_file_end = "";
// добавляется в конце каждой операции
inline constexpr std::string_view operation_file_end = "";
inline constexpr std::string_view typelist_str = "::metel::typelist";

// выключено, чтобы генерировать любые апи. Ошибка будет выдана на этапе попытки записи в такой формат
// static void throw_if_unsupported_content_type(const ir::content_type_t& t) {
//   cxx_support::content_type_e e;
//   if (!efromstr(t.str(), e))
//     throw render_error("content type `{}` unsupported", t.str());
// }

// строка из спецификации -> C++ идентификатор (см. render_options::simple_cxx_identifiers)
inline std::string make_cxx_ident(std::string_view str, const render_options& options) {
  return options.simple_cxx_identifiers ? to_cxx_ident_simple(str) : to_cxx_ident(str);
}

// хедер ответа или части multipart как параметр (для render_parameters)
inline ir::operation_parameter header_as_parameter(const ir::header& h) {
  h.validate();
  return ir::operation_parameter{
      .location = parameter_location::header,
      .name = h.name,
      .schema = h.schema ? *h.schema : ir::make_fundamental_ref(ir::fundamental_e::string),
      .required = h.required,
      .encoding = h.encoding,
      .default_value = h.default_value,
  };
}

// путь файла схемы относительно выходной директории
inline std::string make_file_path(const ir::schema_ref& ref, const render_options& options) {
  // добавляем "/schemas" чтобы всё связанное с типами складывалось отдельно и не засоряло
  // папки operations/webhooks/директорию где это всё лежит. Все типы локализованы в schemas
  // при этом публичные в schemas/components/schemas
  std::string path = ref.spec + "/schemas";
  // сегменты разэкранированы, как и в неймспейсах: /paths/~1login -> paths/_login
  for (std::string_view seg : ref.jsonptr->segments()) {
    path += '/';
    path += make_cxx_ident(jp_unescape_segment(seg), options);
  }
  path += ".hpp";
  return path;
}

// путь файла операции или вебхука
inline std::string make_file_path(const ir::operation& ref, const render_options& options, bool is_webhook) {
  std::string path = ref.spec + (is_webhook ? "/webhooks" : "/operations");
  path += '/';
  path += make_cxx_ident(ref.id, options);
  path += ".hpp";
  return path;
}

// у инлайн-схемы имя - последний сегмент её json pointer (свойство, индекс, "schema"...)
inline std::string get_schema_name(const ir::schema_ref& s, const render_options& options) {
  if (s.name)
    return make_cxx_ident(*s.name, options);
  if (s.jsonptr->segments_count() == 0)
    return std::string(inline_schema_name);
  return make_cxx_ident(s.jsonptr->last_segment(), options);  // last_segment уже без экранирования
}
inline std::string get_schema_name(const ir::schema& s, const render_options& options) {
  return get_schema_name(s.ref(), options);
}

namespace detail {

// экранирование для C++ строкового литерала
inline std::string escape_plain(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        // управляющие символы и прочее странное пишется числовым кодом
        if (c < 0x20 || c == 0x7F) {
          // ровно 3 восьмеричных разряда — escape не "съест" следующий символ
          out += std::format("\\{:03o}", c);
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

}  // namespace detail

// C++ строковый литерал
inline std::string render_string(std::string_view s) {
  return std::format("\"{}\"", detail::escape_plain(s));
}

// static constexpr parse_tag - по нему рантайм определяет вид сгенерированного типа
inline std::string render_parse_tag(cxx_support::parse_tag_e tag) {
  return std::format(
      "\nstatic constexpr svarg::cxx_support::parse_tag_e parse_tag = "
      "svarg::cxx_support::parse_tag_e::{};\n\n",
      e2str(tag));
}

// параметры как поля структуры (для render_object_fields)
inline arrayof<ir::object_field_info> as_object_fields(auto&& params) {
  arrayof<ir::object_field_info> result;
  for (const ir::operation_parameter& p : params)
    result.push_back({.type = p.schema,
                      .name = p.name,
                      .required = p.required.value_or(false),
                      .default_value = p.default_value});
  return result;
}

// единственное поле типа с одним значением и доступ к нему через get()
inline std::string render_special_field(std::string_view type) {
  return std::format(
      "{0} {1};\n\n"
      "[[nodiscard]] constexpr {0}& get() noexcept {{ return {1}; }}\n"
      "[[nodiscard]] constexpr const {0}& get() const noexcept {{ return {1}; }}\n\n",
      type, special_field_name);
}

template <typename Params>
std::string renderer::render_parameters(const Params& params, std::string_view relspec,
                                        const json_pointer& relative) const {
  string s;

  string_map<field_render_info> extra;
  for (const ir::operation_parameter& p : params) {
    // параметры с `content:` кодируются своим медиа типом
    if (p.encoding)
      extra[p.name].encoding = p.encoding->str();
    if (!p.meta)
      continue;
    // explode из meta (OpenAPI), только явно указанный. Поддержаны style по умолчанию (simple в path и
    // header, form в query и cookie), для остальных - предупреждение и умолчание
    if (auto it = p.meta->find(x_param_style); it != p.meta->end()) {
      std::string_view st =
          it->second.value.is_string() ? std::string_view(it->second.value.as_string()) : "";
      bool form_location =
          p.location == parameter_location::query || p.location == parameter_location::cookie;
      if (st != (form_location ? "form" : "simple")) {
        std::println("warning: parameter `{}`: style `{}` is not supported, default style is used", p.name,
                     st);
        continue;
      }
    }
    if (auto it = p.meta->find(x_param_explode); it != p.meta->end() && it->second.value.is_bool())
      extra[p.name].explode = it->second.value.as_bool();
  }
  s += render_object_fields(as_object_fields(params), relspec, relative, /*fields_owner=*/std::nullopt,
                            /*additional_properties=*/std::nullopt, /*part_headers=*/nullptr, &extra);
  return s;
}

}  // namespace svarg::cxx
