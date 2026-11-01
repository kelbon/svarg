#pragma once

#include "svarg/ir/ir_object.hpp"

#include <print>
#include <kelcoro/generator.hpp>

namespace svarg::cxx {

struct render_options {
  // если `false`, то алгоритм преобразования строк в cxx идентификаторы
  // будет гарантировать несовпадение имён, но имена будут иногда более сложные
  bool simple_cxx_identifiers = true;
  // не генерирует ничего, только возвращает список путей сгенерированных файлов
  // сортированных по именам + раширениям
  bool dry_run = false;
  // инлайн-подсхемы типа `X` лежат в неймспейсе `X<suffix>`, например Item_::properties::kind,
  // чтобы неймспейс не конфликтовал с самим типом
  std::string inline_scope_suffix = "_";
  // вставляется в начало каждого сгенерированного заголовка после рантайма, например
  // `#include "my_codecs.hpp"` со специализациями media_codec (см. support/codec.hpp)
  std::string file_prefix;
  // медиа типы тела ответа, которое передаётся потоком (в нижнем регистре, без параметров). Структура такого
  // ответа без тела: сервер отправляет его ctx.stream_response, клиент получает частями в колбэк
  std::vector<std::string> stream_content_types = {"application/octet-stream"};
};

// то, что попадает в field_info поля помимо имени и обязательности
struct field_render_info {
  std::optional<std::string> encoding;  // медиа тип значения
  std::optional<bool> explode;          // массивы/объекты в параметрах, если указано явно
};

struct schema_ref_result {
  std::string typeref;
  bool is_alias = false;
};

struct render_result {
  std::string filename;  // куда складывать
  std::string rendered;  // что складывать
};

struct rendered_with_name {
  std::string struct_name;
  std::string render;
};

// C++ бэкенд: IR -> заголовки C++ и .cpp на каждую спеку. Сгенерированный код - это данные (типы, описания
// полей, медиа типы), логика (де)сериализации - в рантайме support/. Части бэкенда по файлам:
//
//   render.cpp               - вход (render2cxx), запись файлов, неймспейсы
//   render_schemas.cpp       - схемы: типы, поля, зависимости между схемами
//   render_operations.cpp    - операции: параметры, тела, ответы, result_t, extern инстанциации
//   render_server_client.cpp - сервер (маршрутизация) и клиенты
//   render_common.hpp        - общее: имена шаблонов рантайма, идентификаторы, строки, пути файлов
//
// Бэкенд для другого языка можно устроить так же: обход IR, по функции на вид сущности, а логику - в
// рантайм этого языка
struct renderer {
  explicit renderer([[clang::lifetimebound]] const ir::ir_object& ir, render_options opts)
      : options(std::move(opts)), ir(ir) {
    if (ir.schemas)
      for (const ir::schema& s : *ir.schemas)
        schema_locations.emplace(s.spec, std::string(s.jsonptr.escaped()));
    collect_body_encodings();
  }

  // генерирует всё в `output_dir`. Возвращает список сгенерированных путей без дубликатов, сортированный по
  // пути, а затем по расширению файла
  std::vector<fs::path> render2cxx(fs::path output_dir) const;

 private:
  render_options options;
  const ir::ir_object& ir;
  mutable std::vector<fs::path> generated;
  // (spec, json pointer) всех схем: по ним определяется, какие сегменты неймспейса получают суффикс
  std::set<std::pair<std::string, std::string>> schema_locations;
  // (spec, json pointer) схемы тела form/multipart => тело с encoding её свойств. encoding рендерится в
  // описания полей самой схемы (fields[I].encoding), чтобы кодировать её поля без знания о владельце тела
  std::map<std::pair<std::string, std::string>, const ir::body*> body_encodings;

  const ir::schemas_t& schemas() const {
    return *ir.schemas;
  }

  const ir::schema& schemaof(const ir::schema_ref& ref) const {
    assert(!ref.is_fundamental());
    auto it = schemas().find(ref);
    if (it == schemas().end())
      throw render_error("schema {}#{} do not refer to any schema in IR", ref.spec, ref.jsonptr->escaped());
    return *it;
  }

  template <typename... Args>
  void emit_warn(std::format_string<Args...> fmt, Args&&... args) const {
    std::println("warning: {}", std::format(fmt, std::forward<Args>(args)...));
  }

  // ===== render.cpp: файлы и неймспейсы =====

  void write_file(const fs::path& path, std::string_view content) const {
    generated.push_back(path);
    if (options.dry_run)
      return;
    write_into(path, content);
  }
  // пишет все файлы `renders`, возвращает вектор неповторяющихся путей
  std::vector<fs::path> render_all(fs::path output_dir, dd::generator<render_result>) const;
  std::vector<fs::path> render_all_schemas(fs::path output_dir) const;
  std::vector<fs::path> render_all_operations(fs::path output_dir, const std::set<ir::operation>& ops,
                                              bool is_webhooks) const;
  // начало каждого заголовка: pragma once, рантайм, file_prefix
  std::string render_file_begin() const;
  // неймспейс для того, что лежит по `jp` (без последнего сегмента - это имя). Сегменты, которые сами
  // являются схемами, получают inline_scope_suffix
  std::string make_namespace(std::string_view spec, json_pointer jp) const;
  // обращение из relative_spec + relative к spec + jp (общий префикс неймспейсов опускается)
  std::string make_namespace(std::string_view spec, const json_pointer& jp, std::string_view relative_spec,
                             const json_pointer& relative) const;

  // ===== render_schemas.cpp: схемы =====

  // файл схемы: include зависимостей, их предварительные объявления, struct или алиас. Если схема -
  // алиас (тип выражается без своей структуры), файла нет: возвращается пустой путь
  render_result render_schema(const ir::schema&) const;
  // тело struct схемы: parse_tag, поля или единственное поле _implfield, ограничения, требования
  std::string render_struct_content(const ir::schema& s, std::string typeref) const;

  // обращение к типу схемы из неймспейса `relative`: a::b::X или повторённый алиас arrayof<a::b::X>
  schema_ref_result render_schema_ref(const ir::schema_ref&, std::string_view relspec,
                                      const json_pointer& relative) const;
  schema_ref_result render_fundamental_type(const ir::fundamental_e) const;
  // тип единственного поля для комбинаторов, массивов, not; для объектов и енамов - пустая строка
  std::string render_special_field_type(const ir::schema_type&, std::string_view relspec,
                                        const json_pointer& relative) const;
  // список типов через запятую (аргументы oneof<...> и т.п.)
  std::string render_schemas_array(const arrayof<ir::schema_ref>& arr, std::string_view relspec,
                                   const json_pointer& relative) const;
  std::string render_discriminator(const ir::discriminator_desc& d, std::string_view relspec,
                                   const json_pointer& relative) const;

  // поля объекта или структуры параметров: сначала требуемые, затем опциональные, additional properties,
  // описания полей (fields) и доступ по индексу (svarg_field). `part_headers` - хедеры частей multipart
  // (поле становится part<T, H>), `extra` - то, что попадает в описание поля помимо имени
  std::string render_object_fields(const arrayof<ir::object_field_info>& fields, std::string_view relspec,
                                   const json_pointer& relative, std::optional<ir::schema_ref> fields_owner,
                                   std::optional<ir::schema_ref> additional_properties,
                                   const string_map<std::set<ir::header>>* part_headers = nullptr,
                                   const string_map<field_render_info>* extra = nullptr) const;
  // одно поле: T, optional<T>, box<T> для рекурсии, значение по умолчанию
  std::string render_object_field(const ir::object_field_info&, std::string_view relspec,
                                  const json_pointer& relative, bool recursive_field) const;
  std::string render_required_field(const ir::schema_ref&, std::string_view name, std::string_view relspec,
                                    const json_pointer& relative, bool recursive) const;
  std::string render_optional_field(const ir::schema_ref&, std::string_view name, std::string_view relspec,
                                    const json_pointer& relative, bool recursive) const;
  // инициализатор поля значением по умолчанию: литерал или default_value<T, "json">()
  std::string render_default_value(const ir::schema_ref&, const Any& value, std::string_view relspec,
                                   const json_pointer& relative) const;
  // поле типа, который (через другие схемы) содержит владельца: хранится в box
  bool is_recursive_field(const ir::schema_ref& owner, const ir::schema_ref& field_type) const;
  // вложенные структуры хедеров частей multipart (encoding.headers)
  std::string render_part_headers(const string_map<std::set<ir::header>>& part_headers,
                                  std::string_view relspec, const json_pointer& relative) const;
  // заполняет body_encodings из тел всех операций
  void collect_body_encodings();
  const ir::body* find_body_encodings(const ir::schema& s) const;

  // зависимости схем (для include и предварительных объявлений). Только первый уровень, никогда не
  // собирает фундаментальные ссылки, set - для одинакового порядка на всех реализациях
  void collect_mentioned_first_layer(const ir::schema_ref& in_what, ir::schema_refs_t& out) const;
  // имена спек, от которых зависит s
  std::vector<std::string> collect_mentioned_first_layer_specs(const ir::schema_ref& s) const;
  // все зависимости с учётом рекурсии (A требует B, B требует A). Не возвращает `in_what` в `out`, но
  // если тип рекурсивно зависит сам от себя, возвращает true
  bool collect_all_mentioned(const ir::schema_ref& in_what, ir::schema_refs_t& out) const;
  // заменяет ссылки на алиасы и фундаментальные типы их зависимостями (файлов у алиасов нет)
  [[nodiscard]] ir::schema_refs_t dealias(ir::schema_refs_t) const;

  // ===== render_operations.cpp: операции =====

  // файл операции: struct с path_t/query_t/headers_t/cookies_t, телом, ответами, visit_response_status,
  // result_t, и extern инстанциации её (де)сериализации
  render_result render_operation(const ir::operation&, bool is_webhook) const;
  // структура параметров (path_t, query_t, headers_t, cookies_t, хедеры ответа и части multipart):
  // поля и их описания. Определён в render_common.hpp
  template <typename Params>
  std::string render_parameters(const Params& params, std::string_view relspec,
                                const json_pointer& relative) const;
  // одно тело - статические члены владельца и поле body, несколько - обёртки и variant
  std::string render_bodies(const std::set<ir::body>&, std::string_view spec, const json_pointer& jp) const;
  // обёртка тела при нескольких телах: content_type и единственное поле
  std::string render_open_string_enum(const ir::enum_schema&, const ir::schema&) const;
  // тип значения тела: схема, байты (std::string) для тела без схемы медиа типа без встроенного кодека
  std::string render_body_type(const ir::body& body, std::string_view spec, const json_pointer& jp) const;
  rendered_with_name render_body(const ir::body& body, std::string_view spec, const json_pointer& jp) const;
  // struct ответа: статус, хедеры, тела
  rendered_with_name render_response(const ir::response_desc&, std::string_view spec,
                                     const json_pointer& jp) const;
  // явные инстанциации make_request/from_request/send_request и make_response/from_response:
  // extern - в заголовке операции, определения - в .cpp спеки
  std::string render_instantiations(const ir::operation& op, bool is_extern) const;
  // потоковый медиа тип тела ответа (render_options::stream_content_types), пусто - ответ не поток
  std::string_view stream_content_type_of(const ir::response_desc&) const;
  // у операции есть ответ потоком
  bool has_stream_responses(const ir::operation&) const;

  // ===== render_server_client.cpp: сервер и клиенты =====

  // один сервер на спеку со всеми операциями: виртуальные обработчики и маршрутизация handle_request.
  // `source` получает определения для .cpp спеки
  std::string render_server(const std::vector<const ir::operation*>& operations, std::string_view spec,
                            std::string& source) const;
  // клиент на каждый набор серверов операций: send_X для каждой операции
  std::string render_client(const std::vector<const ir::operation*>& operations, std::string_view spec,
                            std::string& source) const;
};

}  // namespace svarg::cxx
