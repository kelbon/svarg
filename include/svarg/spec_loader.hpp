#pragma once

#include <unordered_set>
#include <span>

#include "svarg/openapi.hpp"

#include "svarg/ir/ir_object.hpp"
#include <iostream>

namespace svarg {

struct schema_and_type {
  ir::type type;
  schema_object schema;
};

// загружает спецификацию, затем разрешает все ссылки в ней
// и разрешает видимо сразу в IR структуру (которая позже преобразуется в json)
// Загружается спека одна, но она тащит за собой все другие (в ссылках и тд)
// каждая встреченная спека также полностью загружается и складывается в кеш
// а дальше generate уже идёт конкретных спек (только из тех что загружены), т.е. не все иногда нужны
// например одна спека ссылается на другие, но paths и тд из других не нужны
// АХ да, надо разделить явно на fronend / IR / backend
struct spec_loader {
 private:
  // путь до спеки относительно specs_dir -> её распаршенный объект с зарезолвленными ссылками без #
  // и загруженные явно и упомянутые в загруженных
  std::unordered_map<std::filesystem::path, openapi_object> specs;
  // путь до спек относительно specs_dir, для которых вызвано load_spec явно. Помимо них в specs ещё могут
  // быть просто упомянутые, но не те IR которых пользователь хотел получить
  std::unordered_set<std::filesystem::path> loaded_specs;
  svarg_options opts;
  // путь относительно которого в IR написаны все пути до спецификаций
  std::filesystem::path specs_dir;
  jsons_cache jcache;

 public:
  void load_spec(std::filesystem::path s) {
    if (!std::filesystem::exists(s) || !std::filesystem::is_regular_file(s))
      throw parse_error("path `{}` do not refer to file", s.generic_string());
    if (specs_dir.empty())
      specs_dir = s.parent_path();
    s = std::filesystem::relative(s, specs_dir);
    if (!specs.contains(s))
      specs[s] = do_parse_spec_file(s);
    loaded_specs.insert(std::move(s));
  }

  // возвращает IR всех загруженных спецификаций
  ir::ir_object generate();

  std::filesystem::path get_specs_dir() const {
    return specs_dir;
  }
  void set_specs_dir(std::filesystem::path p) {
    specs_dir = std::filesystem::absolute(std::move(p));
    // TODO ремапить все пути
    assert(specs.empty() && loaded_specs.empty());
  }

  // возвращает первый из существующих файлов из списка specs_dir/filename, -I dirs/filename...
  // используется для разрешений ссылок
  // возвращаемый путь всегда относительно specs_dir
  [[nodiscard]] std::filesystem::path find_existing_file(std::filesystem::path filepart) const {
    filepart = std::filesystem::relative(filepart, specs_dir);
    std::filesystem::path p = specs_dir / filepart;
    if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p))
      return filepart;
    // TODO -I
    // TODO добавить в ошибку список проверенных путей
    throw parse_error("no such file: `{}`", filepart.generic_string());
  }

  // возвращает тип, но и вставляет его в types (если его там не было)
  schema_and_type resolve_ref(reference_object obj, std::filesystem::path file,
                              std::unordered_set<ir::type>& types);

  // file - относительно specs dir, jsonptr должен указывать на место где лежит schema
  // если встречает инлайн схемы - складывает их в types
  ir::object_type make_object_type(schema_object const& schema, const std::filesystem::path& file,
                                   std::string_view jsonptr, std::unordered_set<ir::type>& types);

  template <typename... Args>
  void emit_warn(const std::filesystem::path& where, std::string_view jsonptr,
                 std::format_string<Args...> fmt, Args&&... args) {
    std::cout << std::format("in {}#{}. warning: {}\n", where.generic_string(), jsonptr,
                             std::format(fmt, std::forward<Args>(args)...));
  }

 private:
  const boost::json::value& load_as_json(std::filesystem::path const& p) {
    // spec_dir != cwd
    // не включен ассёрт, так как файловую систему могут поменять
    // assert(std::filesystem::is_directory(specs_dir));
    return jcache.load(specs_dir / p);
  }
  openapi_object do_parse_spec_file(std::filesystem::path spec) {
    // подменяем кеш, чтобы в нём не было незарезолвленных сырых ссылок
    boost::json::value& v = jcache.load_mutable(specs_dir / spec);
    // resolve_raw_references НЕ использует jcache, так что это безопасно
    v = resolve_raw_references(std::move(v), opts);
    return parse_spec(std::as_const(v), opts);
  }
  template <typename>
  friend struct vtor;

  ir::ir_object visit_and_generate_ir(const openapi_object& obj, const std::filesystem::path& obj_file);
};

}  // namespace svarg
