#pragma once

#include <unordered_set>
#include <iostream>

#include "svarg/openapi.hpp"

#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/combined_hash.hpp"
#include "svarg/utils/strpair.hpp"

namespace svarg {

struct resolved_schema {
  ir::schema_ref ir_schema;
  schema_object front_schema;
};

struct schema_or_ref {
  std::variant<ir::schema_ref, ir::schema> data;

  ir::schema_ref ref() const& {
    if (auto* r = std::get_if<ir::schema_ref>(&data))
      return *r;
    else
      return std::get<ir::schema>(data).ref();
  }
  ir::schema_ref ref() const&& {
    return ref();
  }
  ir::schema_ref ref() & {
    return std::as_const(*this).ref();
  }
  ir::schema_ref ref() && {
    if (auto* r = std::get_if<ir::schema_ref>(&data))
      return std::move(*r);
    else
      return std::move(std::get<ir::schema>(data)).ref();
  }
};

// TODO общий интерфейс для фронтенда и это реализация для опенапи
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
  std::unordered_map<fs::path, openapi_object> specs;
  // путь до спек относительно specs_dir, для которых вызвано load_spec явно. Помимо них в specs ещё могут
  // быть просто упомянутые, но не те IR которых пользователь хотел получить
  std::unordered_set<fs::path> loaded_specs;
  svarg_options opts;
  // путь относительно которого в IR написаны все пути до спецификаций
  fs::path specs_dir;
  jsons_cache jcache;
  // схемы создаваемые прямо сейчас (scope + name), чтобы избегать рекурсии
  std::unordered_set<strpair> schemas_in_progress;

 public:
  const svarg_options& get_options() const noexcept {
    return opts;
  }
  void load_spec(fs::path s) {
    if (!fs::exists(s) || !fs::is_regular_file(s))
      throw parse_error("path `{}` do not refer to file", s.generic_string());
    if (specs_dir.empty())
      specs_dir = s.parent_path();
    s = fs::relative(s, specs_dir);
    if (!specs.contains(s))
      specs[s] = do_parse_spec_file(s);
    loaded_specs.insert(std::move(s));
  }

  // возвращает IR всех загруженных спецификаций
  ir::ir_object generate();

  fs::path get_specs_dir() const {
    return specs_dir;
  }
  void set_specs_dir(fs::path p) {
    specs_dir = fs::absolute(std::move(p));
    // TODO ремапить все пути
    assert(specs.empty() && loaded_specs.empty());
  }

  // reffile - файл где написана ссылка
  // filefromref - файл до # в ссылке (в том числе пустой)
  [[nodiscard]] fs::path find_file_from_ref(fs::path filefromref, const fs::path& reffile) const {
    // даже для абсолютных делаем relative чтобы правильно запрашивать файлы в кеше (единообразно)
    // поддерживаем абсолютные пути не смотря на то что их вроде как нельзя в $ref
    // т.к. имплементация сама создаёт такие ссылки внутри resolve_ref
    if (!filefromref.is_absolute())
      filefromref = !filefromref.empty() ? reffile.parent_path() / filefromref : reffile;
    filefromref = fs::relative(fs::absolute(filefromref), specs_dir);
    return find_existing_file(filefromref);
  }
  // возвращает первый из существующих файлов из списка specs_dir/filename, -I dirs/filename...
  // используется для разрешений ссылок
  // возвращаемый путь всегда относительно specs_dir
  [[nodiscard]] fs::path find_existing_file(fs::path filepart) const {
    filepart = fs::relative(filepart, specs_dir);
    fs::path p = specs_dir / filepart;
    if (fs::exists(p) && fs::is_regular_file(p))
      return filepart;
    // TODO -I
    // TODO добавить в ошибку список проверенных путей
    throw parse_error("no such file: `{}`", filepart.generic_string());
  }

  // возвращает тип, но и вставляет его в types (если его там не было)
  resolved_schema resolve_schema_ref(reference_object obj, fs::path file,
                                     std::unordered_set<ir::schema>& types);

  callback_object resolve_callback(reference_or<callback_object>, fs::path file);
  path_item_object resolve_path_item_object(reference_or<path_item_object> item, fs::path file);

  // file - относительно specs dir, jsonptr должен указывать на место где лежит schema
  // если встречает инлайн схемы - складывает их в types
  ir::object_schema make_object_type(schema_object const& schema, const fs::path& file,
                                     std::string_view jsonptr, std::unordered_set<ir::schema>& types);

  template <typename... Args>
  void emit_warn(const fs::path& where, std::string_view jsonptr, std::format_string<Args...> fmt,
                 Args&&... args) {
    std::cout << std::format("in {}#{}. warning: {}\n", where.generic_string(), jsonptr,
                             std::format(fmt, std::forward<Args>(args)...));
  }

  schema_or_ref schema_lowering(const schema_object& schema, const fs::path& file, std::string_view jsonptr,
                                std::optional<std::string_view> name,
                                std::unordered_set<ir::schema>& schemas);

  // name - operationId для операций, имя калбека/вебхука для калбека или вебхука
  // path - путь относительно сервера для операции, runtime expr для калбека
  // spec - где определена операция. Даже если операция была ссылкой, она могла перекрыть какие-то филды
  // и поэтому стала новой операцией в том файле где написана ссылка
  ir::operation path_item_object_lowering(std::string_view path, fs::path spec,
                                          std::optional<std::string_view> name, path_item_object);

 private:
  template <typename T>
  friend auto resolve_ref_to(reference_object ref, fs::path file, auto&& invocable, spec_loader& loader);
  const boost::json::value& load_as_json(fs::path const& p) {
    // spec_dir != cwd
    // не включен ассёрт, так как файловую систему могут поменять
    // assert(fs::is_directory(specs_dir));
    return jcache.load(specs_dir / p);
  }
  openapi_object do_parse_spec_file(fs::path spec) {
    // подменяем кеш, чтобы в нём не было незарезолвленных сырых ссылок
    boost::json::value& v = jcache.load_mutable(specs_dir / spec);
    // resolve_raw_references НЕ использует jcache, так что это безопасно
    v = resolve_raw_references(std::move(v), opts);
    return parse_spec(std::as_const(v), opts);
  }
  template <typename>
  friend struct vtor;

  ir::ir_object visit_and_generate_ir(const openapi_object& obj, const fs::path& obj_file);
};

}  // namespace svarg
