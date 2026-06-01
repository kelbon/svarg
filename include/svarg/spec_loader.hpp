#pragma once

#include <unordered_set>
#include <span>

#include "svarg/openapi.hpp"

#include "svarg/ir/ir_object.hpp"

namespace svarg {

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
  // unordered_map<std::filesystem::path, svarg_ir> irs;
  svarg_options opts;
  // путь относительно которого в IR написаны все пути до спецификаций
  std::filesystem::path specs_dir;
  jsons_cache jcache;

 public:
  void load_spec(std::filesystem::path s) {
    s = std::filesystem::relative(s, specs_dir);
    if (specs_dir.empty())
      specs_dir = s.parent_path();
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

 private:
  openapi_object do_parse_spec_file(std::filesystem::path spec) {
    boost::json::value v = jcache.load(std::move(spec));
    v = resolve_raw_references(std::move(v), opts);
    return parse_spec(std::move(v), opts);
  }
  template <typename>
  friend struct vtor;
  // используется только в .cpp
  template <typename T>
  ir::type resolve_ref_to(reference_object obj, std::filesystem::path file);
  ir::ir_object visit_and_generate_ir(const openapi_object& obj, const std::filesystem::path& obj_file);
};

}  // namespace svarg
