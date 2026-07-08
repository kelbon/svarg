#pragma once

#include "svarg/ir/ir_object.hpp"

#include <print>
#include <kelcoro/generator.hpp>

namespace svarg::cxx {

struct render_options {
  // если `false`, то алгоритм преобразования строк в cxx идентификаторы
  // будет гарантировать несовпадение имён, но имена будут иногда более сложные
  bool simple_cxx_identifiers = true;
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

// TODO вызов этих операций чтобы рендерить все схемы + инструмент для превращения IR в С++.
// может быть функцию которая напрямую делает всё в одном процессе дополнительно
// но уже сейчас почти можно сделать IR -> CXX + driver openapi2cxx (хотя там можно общий интерфейс
// использовать бекенда и фронтенда и только дефолт сделать openapi/c++)
// TODO операции!
// TODO написать ридми IR с полным его описанием

struct renderer {
  explicit renderer([[clang::lifetimebound]] const ir::ir_object& ir, render_options opts)
      : options(std::move(opts)), ir(ir) {
  }

  void render2cxx(fs::path output_dir) const;

 private:
  render_options options;
  const ir::ir_object& ir;

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

  bool is_recursive_field(const ir::schema& s, const ir::schema_ref& field_type) const;
  std::string render_schemas_array(const arrayof<ir::schema_ref>& arr, std::string_view relspec,
                                   const json_pointer& relative) const;
  std::string render_struct_content(const ir::schema& s, std::string typeref) const;

  schema_ref_result render_fundamental_type(const ir::fundamental_e) const;

  std::string render_required_field(const ir::schema_ref&, std::string_view name, std::string_view relspec,
                                    const json_pointer& relative) const;
  std::string render_optional_field(const ir::schema_ref&, std::string_view name, std::string_view relspec,
                                    const json_pointer& relative, bool recursive) const;

  // генерирует поля (сначала требуемые, затем опциональные), is_required, using additional_properties = ..
  // и если есть дополнительные поля, генерирует поле _additional_properties
  // TODO? геттеры и сеттеры
  std::string render_object_fields(const arrayof<ir::object_field_info>& fields, std::string_view relspec,
                                   const json_pointer& relative, std::optional<ir::schema_ref> fields_owner,
                                   std::optional<ir::schema_ref> additional_properties) const;

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

  // убирает из схем все ссылки на алиасы или фундаменталы, заменяя их их зависимостями
  [[nodiscard]] ir::schema_refs_t dealias(ir::schema_refs_t) const;
};

}  // namespace svarg::cxx
