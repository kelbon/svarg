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

// TODO написать ридми IR с полным его описанием

struct renderer {
  explicit renderer([[clang::lifetimebound]] const ir::ir_object& ir, render_options opts)
      : options(std::move(opts)), ir(ir) {
    if (ir.schemas)
      for (const ir::schema& s : *ir.schemas)
        schema_locations.emplace(s.spec, std::string(s.jsonptr.escaped()));
    collect_body_encodings();
  }

  // возвращает список сгенерированных путей без дубликатов, сортированный по пути,
  // а затем по расширению файла
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

  void collect_body_encodings();
  std::string render_file_begin() const;
  std::string render_part_headers(const string_map<std::set<ir::header>>& part_headers,
                                  std::string_view relspec, const json_pointer& relative) const;
  const ir::body* find_body_encodings(const ir::schema& s) const;

  // неймспейс для того, что лежит по `jp` (без последнего сегмента - это имя)
  std::string make_namespace(std::string_view spec, json_pointer jp) const;
  // обращение из relative_spec + relative к spec + jp
  std::string make_namespace(std::string_view spec, const json_pointer& jp, std::string_view relative_spec,
                             const json_pointer& relative) const;

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

  bool is_recursive_field(const ir::schema_ref& owner, const ir::schema_ref& field_type) const;
  std::string render_schemas_array(const arrayof<ir::schema_ref>& arr, std::string_view relspec,
                                   const json_pointer& relative) const;
  std::string render_struct_content(const ir::schema& s, std::string typeref) const;

  schema_ref_result render_fundamental_type(const ir::fundamental_e) const;

  std::string render_required_field(const ir::schema_ref&, std::string_view name, std::string_view relspec,
                                    const json_pointer& relative, bool recursive) const;
  std::string render_default_value(const ir::schema_ref&, const Any& value, std::string_view relspec,
                                   const json_pointer& relative) const;
  std::string render_optional_field(const ir::schema_ref&, std::string_view name, std::string_view relspec,
                                    const json_pointer& relative, bool recursive) const;

  // генерирует поля (сначала требуемые, затем опциональные), is_required, using additional_properties = ..
  // и если есть дополнительные поля, генерирует поле _additional_properties
  // TODO? геттеры и сеттеры
  std::string render_object_fields(const arrayof<ir::object_field_info>& fields, std::string_view relspec,
                                   const json_pointer& relative, std::optional<ir::schema_ref> fields_owner,
                                   std::optional<ir::schema_ref> additional_properties,
                                   const string_map<std::set<ir::header>>* part_headers = nullptr,
                                   const string_map<field_render_info>* extra = nullptr) const;

  std::string render_object_field(const ir::object_field_info&, std::string_view relspec,
                                  const json_pointer& relative, bool recursive_field) const;

  std::string render_discriminator(const ir::discriminator_desc& d, std::string_view relspec,
                                   const json_pointer& relative) const;

  // собирает только первый уровень зависимостей
  // никогда не собирает фундаментальные ссылки
  // set используется для сохранения порядка на всех реализациях
  void collect_mentioned_first_layer(const ir::schema_ref& in_what, ir::schema_refs_t& out) const;

  // собирает список имён спек от которых зависит s
  std::vector<std::string> collect_mentioned_first_layer_specs(const ir::schema_ref& s) const;

  // корректно обрабатывает рекурсию (A требует A или A требует B C D и B требует A и тд)
  // не возвращает никогда `in_what` в `out`
  // но если рекурсивно тип зависит сам от себя возвращается true
  bool collect_all_mentioned(const ir::schema_ref& in_what, ir::schema_refs_t& out) const;

  // генерирует обращение к тому что в schema_type
  // для объектов и енамов возвращает пустую строку
  std::string render_special_field_type(const ir::schema_type&, std::string_view relspec,
                                        const json_pointer& relative) const;

  // генерирует обращение к имени типа обозначаемого схемой. Если это алиас, то он будет повторён
  // т.е. возможно это a::b::mytype, а может и arrayof<arrayof<a::b::X>>
  // `relative` - в каком неймспейсе сейчас находимся
  schema_ref_result render_schema_ref(const ir::schema_ref&, std::string_view relspec,
                                      const json_pointer& relative) const;

  // рендерит весь файл со схемой
  // если схема алиас - то возвращается пустой путь, генерировать файл не нужно
  render_result render_schema(const ir::schema&) const;
  render_result render_operation(const ir::operation&, bool is_webhook) const;

  // возвращает вектор неповторяющихся путей файлов которые были записаны
  std::vector<fs::path> render_all(fs::path output_dir, dd::generator<render_result>) const;
  std::vector<fs::path> render_all_schemas(fs::path output_dir) const;
  std::vector<fs::path> render_all_operations(fs::path output_dir, const std::set<ir::operation>& ops,
                                              bool is_webhooks) const;

  std::string render_bodies(const std::set<ir::body>&, std::string_view spec, const json_pointer& jp) const;

  rendered_with_name render_response(const ir::response_desc&, std::string_view spec,
                                     const json_pointer& jp) const;

  rendered_with_name render_body(const ir::body& body, std::string_view spec, const json_pointer& jp) const;

  friend std::string render_parameters(const auto& params, std::string_view spec, const json_pointer& jp,
                                       const renderer&);

  // один сервер на спеку со всеми операциями
  // `source` получает определения для .cpp спеки
  std::string render_server(const std::vector<const ir::operation*>& operations, std::string_view spec,
                            std::string& source) const;
  std::string render_instantiations(const ir::operation& op, bool is_extern) const;

  std::string render_client(const std::vector<const ir::operation*>& operations, std::string_view spec,
                            std::string& source) const;

  // убирает из схем все ссылки на алиасы или фундаменталы, заменяя их их зависимостями
  [[nodiscard]] ir::schema_refs_t dealias(ir::schema_refs_t) const;

  void write_file(const fs::path& path, std::string_view content) const {
    generated.push_back(path);
    if (options.dry_run)
      return;
    write_into(path, content);
  }
};

}  // namespace svarg::cxx
