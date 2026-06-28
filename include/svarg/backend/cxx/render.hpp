#pragma once

#include "svarg/ir/ir_object.hpp"

#include <print>

namespace svarg::cxx {

struct render_options {
  // если `false`, то алгоритм преобразования строк в cxx идентификаторы
  // будет гарантировать несовпадение имён, но имена будут иногда более сложные
  bool simple_cxx_identifiers = true;
  // используется для опциональных полей
  std::string optional_str = "optional";
  // используется также как optional, но когда поля рекурсивные (т.е. нельзя сделать optional)
  std::string box_str = "box";
  // название шаблона куда подставлять аргументы allOf
  std::string allof_str = "allof";
  std::string anyof_str = "anyof";
  std::string oneof_str = "oneof";
  std::string array_str = "arrayof";
  // куда вставлять аргумент 'not' из openapi
  std::string not_str = "not_a";
  // для схемы без имени. Она будет в отдельном уникальном неймспейсе с этим именем
  std::string inline_schema_name = "schema";
  // добавляется в начале каждой схемы
  std::string schema_type_begin = R"(
#pragma once

#include "common_types.hpp"

)";
  // добавляется в конце каждой схемы
  std::string schema_file_end = "";
  // используется как имя когда есть полутривиальные объекты с одним полем, которые не алиасы только потому
  // что у них есть дополнительные требования или дискриминатор и тд
  // например итоговая структура будет
  // struct Name {
  //    anyof<A, B, C> <special_field_name>; using
  //    required_schemas = <typelist_str><...>:
  // };
  std::string special_field_name = "data";
  std::string typelist_str = "typelist";
};

struct schema_ref_result {
  std::string typeref;
  bool is_alias = false;
};

// рендерит весь файл со схемой
struct render_schema_result {
  std::string filename;         // куда складывать
  std::string rendered_schema;  // что складывать
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

  template <typename... Args>
  void emit_warn(std::format_string<Args...> fmt, Args&&... args) const {
    std::println("warning: {}", std::format(fmt, std::forward<Args>(args)...));
  }

  bool is_recursive_field(const ir::schema& s, const ir::schema_ref& field_type) const;
  std::string render_schemas_array(const array<ir::schema_ref>& arr, const json_pointer& relative) const;
  std::string render_struct_content(const ir::schema& s, std::string typeref) const;

  schema_ref_result render_fundamental_type(const ir::fundamental_e) const;

  std::string render_object_field(const ir::schema& s, const ir::object_field_info&,
                                  const json_pointer& relative) const;

  std::string render_discriminator(const ir::discriminator_desc& d, const json_pointer& relative) const;

  // корректно обрабатывает рекурсию (A требует A или A требует B C D и B требует A и тд)
  // собирает только первый уровень зависимостей
  // никогда не собирает фундаментальные ссылки
  // set используется для сохранения порядка на всех реализациях
  void collect_mentioned_first_layer(const ir::schema_ref& in_what, ir::schema_refs_t& out) const;

  // собирает список имён спек от которых зависит s
  std::vector<std::string> collect_mentioned_first_layer_specs(const ir::schema_ref& s) const;

  // не возвращает никогда `in_what` в `out`
  // но если рекурсивно тип зависит сам от себя возвращается true
  bool collect_all_mentioned(const ir::schema_ref& in_what, ir::schema_refs_t& out) const;

  // генерирует обращение к имени типа обозначаемого схемой. Если это алиас, то он будет повторён
  // т.е. возможно это a::b::mytype, а может и arrayof<arrayof<a::b::X>>
  // `relative` - в каком неймспейсе сейчас находимся
  schema_ref_result render_schema_ref(const ir::schema_ref&, const json_pointer& relative) const;

  render_schema_result render_schema(const ir::schema&) const;

  void render_all_schemas(fs::path output_dir) const;
};

}  // namespace svarg::cxx
