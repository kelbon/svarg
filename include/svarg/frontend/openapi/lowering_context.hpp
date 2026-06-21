#pragma once

#include <unordered_set>
#include <print>
#include <utility>

#include "svarg/frontend/openapi/openapi.hpp"

#include "svarg/ir/ir_object.hpp"
#include "svarg/utils/json_pointer.hpp"
#include "svarg/utils/strpair.hpp"
#include "svarg/utils/forward_like.hpp"

namespace svarg {

struct resolved_schema {
  ir::schema_ref ir_schema;
  schema_object front_schema;
};

struct schema_or_ref {
  std::variant<ir::schema_ref, ir::schema> data;

  template <typename Me>
  ir::schema_ref ref(this Me&& self) {
    if (auto* r = std::get_if<ir::schema_ref>(&self.data))
      return noadl::forwardLike<Me>(*r);
    else
      return std::get<ir::schema>(noadl::forwardLike<Me>(self.data)).ref();
  }
};

template <typename T>
struct object_and_path {
  T object;
  fs::path path;  // путь где лежал в итоге объект
  json_pointer jsonptr;
};

struct lowering_context {
  openapi_options opts;

 private:
  // путь относительно которого в IR написаны все пути до спецификаций
  fs::path specs_dir;

  // не используется напрямую, только через load_as_json
  jsons_cache jcache;
  // схемы создаваемые прямо сейчас (scope + name), чтобы избегать рекурсии
  std::unordered_set<strpair> schemas_in_progress;

  // просто хранится здесь как в контексте
  ir::ir_object result_ir;

 public:
  // принимает путь до директории спек и пути до всех спецификаций кодогенерацию для которых нужно сделать
  // пути могут быть или относительно spec dir или абсолютные
  // Note: инклуд директории из опций распространяются на specs
  [[nodiscard]] ir::ir_object generate_specs(fs::path specsdir, std::vector<fs::path> specs2generate,
                                             openapi_options options = {}) {
    // подготовка
    if (!fs::exists(specsdir) || !fs::is_directory(specsdir))
      throw parse_error("specs directory not exist, provided path: {}", specsdir.generic_string());
    specs_dir = fs::canonical(specsdir);
    std::unordered_set<fs::path> loaded_specs;
    std::unordered_map<fs::path, openapi_object> specs;

    // парсинг спек

    for (fs::path& s : specs2generate) {
      s = find_existing_file(std::move(s), specs_dir);
      if (!specs.contains(s)) {
        specs[s] = do_parse_spec_file(s);
        loaded_specs.insert(std::move(s));
      }
    }

    // перевод в IR

    for (auto& p : loaded_specs) {
      try {
        // результат собирается в this->result_ir
        visit_and_generate_ir(specs.at(p), p);
      } catch (std ::exception& e) {
        throw parse_error("{}/{}", p.generic_string(), e.what());
      }
    }
    return std::move(result_ir);
  }

  // reffile - файл где написана ссылка
  // filefromref - файл до # в ссылке (в том числе пустой)
  [[nodiscard]] fs::path find_file_from_ref(fs::path filefromref, const fs::path& reffile) const {
    // даже для абсолютных делаем relative чтобы правильно запрашивать файлы в кеше (единообразно)
    // поддерживаем абсолютные пути не смотря на то что их вроде как нельзя в $ref
    // т.к. имплементация сама создаёт такие ссылки внутри resolve_ref
    if (filefromref.empty())  // этот файл точно существует
    {
      if (reffile.is_absolute())
        return fs::relative(reffile, specs_dir);
      assert(fs::exists(specs_dir / reffile));
      return reffile;
    }
    if (filefromref.is_absolute())
      return fs::relative(fs::canonical(filefromref), specs_dir);
    return find_existing_file(filefromref, reffile);
  }

  // возвращает первый из существующих файлов из списка specs_dir/filename, -I dirs/filename...
  // используется для разрешений ссылок
  // возвращаемый путь всегда относительно specs_dir
  // `where` - в каком контексте ищется файл (относительно cwd).
  // Т.е. первый проверенный путь будет - локально рядом с ним
  [[nodiscard]] fs::path find_existing_file(fs::path filepart, const fs::path& where) const {
    filepart = filepart.lexically_normal();
    fs::path file = where.lexically_normal().parent_path() / filepart;
    if (fs::exists(file) && fs::is_regular_file(file))
      return fs::relative(file, specs_dir);
    file = specs_dir / filepart;
    if (fs::exists(file) && fs::is_regular_file(file))
      return filepart;
    for (auto&& dir : opts.include_dirs) {
      file = dir / filepart;
      if (fs::exists(file) && fs::is_regular_file(file))
        return fs::relative(file, specs_dir);
    }
    throw parse_error("no such file: `{}`", filepart.generic_string());
  }

  // возвращает тип, но и вставляет его в ir.schemas (если его там не было)
  resolved_schema resolve_schema_ref(reference_object obj, fs::path file, json_pointer jp);

  // file - относительно specs dir, jp должен указывать на место где лежит schema
  // если встречает инлайн схемы - складывает их в types
  ir::object_schema make_object_type(schema_object const& schema, const fs::path& file, json_pointer jp);

  template <typename... Args>
  void emit_warn(const fs::path& where, std::string_view jp, std::format_string<Args...> fmt,
                 Args&&... args) {
    std::print("in {}#{}. warning: {}\n", where.generic_string(), jp,
               std::format(fmt, std::forward<Args>(args)...));
  }

  schema_or_ref schema_lowering(const schema_object& schema, const fs::path& file, json_pointer,
                                std::optional<std::string_view> name);

  // name - operationId для операций, имя калбека/вебхука для калбека или вебхука
  //    используется как базовое имя, если нет operationId. Н-р метод складывается с именем и получается имя
  //    для сгенерированной операции
  // path - путь относительно сервера для операции, runtime expr для калбека
  // resolve_ref_file - путь относительно которого резолвить ссылки.
  //    Может не совпадать с path, так как операция загружена из другого файла, но декларирована то здесь
  // spec - где определена операция. Даже если операция была ссылкой, она могла перекрыть какие-то филды
  //    и поэтому стала новой операцией в том файле где написана ссылка
  // jp - путь до path item в объекте апи
  // Возвращает мапу METHOD => operation
  string_map<ir::operation> path_item_object_lowering(std::string_view path, fs::path spec,
                                                      fs::path resolve_ref_file, json_pointer jp,
                                                      std::string_view name, path_item_object);
  // TODO как-то объединить везде file + jsonptr, везде прокидывается же абсолютно

  ir::operation_parameter parameter_lowering(const parameter_object& param, fs::path file, json_pointer,
                                             std::string_view path);
  std::set<ir::body> request_body_lowering(request_body_object, fs::path file, json_pointer jp);
  ir::response_desc response_lowering(std::optional<std::string_view> statusstr, const response_object& rsp,
                                      fs::path file, json_pointer);
  ir::discriminator_desc discriminator_lowering(const discriminator_object& d,
                                                const array<ir::schema_ref>& schemas);
  ir::body media_type_object_lowering(const media_type_object&, fs::path file, json_pointer,
                                      std::string_view content_type);

  void replace_in_cache(fs::path const& p, const json_pointer& jp, boost::json::value newvalue) {
    boost::json::value& v = jcache.load_mutable(specs_dir / p);
    boost::json::value* refered = jp.access(v);
    if (!refered) {
      // так-то это assert, но файловую систему могли изменить и всё такое
      throw semantic_error("cannot replace value, json pointer `{}` does not refer to object in file `{}`",
                           jp.escaped(), p.generic_string());
    }
    *refered = std::move(newvalue);
  }
  const boost::json::value& load_as_json(fs::path const& p) {
    // spec_dir != cwd
    // не включен ассёрт, так как файловую систему могут поменять
    // assert(fs::is_directory(specs_dir));
    return jcache.load(specs_dir / p);
  }

 private:
  template <typename T>
  friend auto resolve_ref_to(reference_object ref, fs::path file, auto&& invocable, lowering_context&);
  openapi_object do_parse_spec_file(fs::path spec) {
    // подменяем кеш, чтобы в нём не было незарезолвленных сырых ссылок
    boost::json::value& v = jcache.load_mutable(specs_dir / spec);
    // resolve_raw_references НЕ использует jcache, так что это безопасно
    v = resolve_raw_references(std::move(v), specs_dir / spec, opts);
    return parse_spec(std::as_const(v), opts);
  }

  void visit_and_generate_ir(const openapi_object& obj, const fs::path& obj_file);
};

}  // namespace svarg
