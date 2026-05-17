
#include <boost/json/value.hpp>
#include "boost/json/parse.hpp"
#include "svarg/openapi.hpp"
#include "svarg/utils/json_pretty.hpp"

namespace svarg {

// TODO план

// препроцессирование
// TODO сериализацию попросить клауда написать, это изи (сериализация нужна для тестирования
// препроцессирования) тест на парс спеки объект содержащий сразу все спеки нужные резолв ссылок на нём

// если в ref нет # (разделителя на путь до файла и json поинтер) это означает include json/yaml по URI
// делает блокирующий GET запрос по URL с/без SSL (url должно начинаться с http:// или https://)
// а затем парсит ответ как json
static boost::json::value request_refered_object(std::string_view url) {
  // TODO
#if 0
  // TODO кеш этих запросов url -> boost::json::value
  // TODO если там .yaml то надо парсить как yaml
  bool is_yaml = url.ends_with(".yaml") || url.ends_with(".yml");
  // TODO get request etc
  if (is_yaml) {
    std::string json = yaml2json(rsp.body);
    // TODO
  }
  // TODO смотреть внутрь объекта и резолвить дальше, если есть ссылки, при этом ref должны быть относительно
  // нового объекта
#endif
  return {};
}

struct load_raw_ref_return {
  boost::json::value value;
  // путь до файла с value
  std::filesystem::path path;
};

// `url` предполагается без #, просто ссылка на файл
// `curfile` должен быть относительным путём или URL (или также может быть URL, если загрузили файл до этого)
static load_raw_ref_return load_raw_refered_object(std::string_view url, std::filesystem::path curfile,
                                                   const svarg_options& options) {
  curfile = curfile.lexically_normal();
  assert(!url.contains('#'));
  auto is_net_ref = [](std::string_view s) { return s.starts_with("https://") || s.starts_with("http://"); };
  // абсолютные ссылки в сеть
  if (is_net_ref(url)) {
    if (!options.resolve_url_refs) {
      throw parse_error(
          "loading external refs disabled due to options - resolve_url_refs = false by default. ref: `{}`, "
          "path: `{}`",
          url, curfile.string());
    }
    return load_raw_ref_return(request_refered_object(url), url);
  }
  // до этого загрузили из сети, но сейчас это уже внутри загруженного файла локальная ссылка
  if (is_net_ref(curfile.string())) {
    // странно что мы уже загружали и опять, но обработаем на всякий
    if (!options.resolve_url_refs) {
      throw parse_error(
          "loading external refs disabled due to options - resolve_url_refs = false by default. ref: `{}`, "
          "path: `{}`",
          url, curfile.string());
    }
    std::filesystem::path path = curfile.parent_path() / url;
    return load_raw_ref_return(request_refered_object(path.string()), path);
  }
  boost::json::value result;
  std::filesystem::path path = curfile.parent_path() / url;
  pass_as_json(path, [&](std::string_view data) { result = boost::json::parse(data); });
  return load_raw_ref_return(std::move(result), std::move(path));
}

struct raw_ref {
  boost::json::value* ref = nullptr;
  boost::json::value* parent = nullptr;
  std::string value;
};

// собирает все $ref в значении которых нет # и возвращает массив объектов/массивов которые их содержат (а не
// сам объект ссылки)
static void collect_raw_refs(boost::json::value* v, std::vector<raw_ref>& refs) {
  if (v->is_array()) {
    for (auto&& elem : v->as_array())
      collect_raw_refs(&elem, refs);
  } else if (v->is_object()) {
    auto& obj = v->as_object();
    for (auto&& [key, value] : v->as_object()) {
      // поля x- могут содержать любую дичь, бессмысленно ссылки там резолвить
      // например здесь вставляют .php / C# код в примеры
      // https://github.com/Redocly/openapi-starter/blob/main/openapi/paths/echo.yaml
      // с другой стороны есть x- хедеры, где тоже могут быть ссылки, их нельзя исключать
      // так что дурачки с расширениями идут лесом
      // if (key.starts_with("x-"))
      //  continue;
      if (key == "$ref") {
        if (!value.is_string())
          throw parse_error("$ref value must be string, got: `{}`", e2str(value.kind()));
        auto&& str = value.get_string();
        if (auto pos = str.find('#'); pos != str.npos)
          continue;
        refs.push_back(raw_ref{.ref = &obj["$ref"], .parent = v, .value = std::string(str)});
      } else {
        collect_raw_refs(&value, refs);
      }
    }
  }
}

// если depth дошло до 0, то больше погружаться нельзя
// ссылки разрешаются относительно curfile
// возвращает true когда ссылок больше нет
static void include_raw_references(boost::json::value& v, std::filesystem::path curfile, size_t depth,
                                   const svarg_options& options) {
  std::vector<raw_ref> refs;
  collect_raw_refs(&v, refs);
  if (refs.empty())
    return;
  if (depth == 0) {
    std::string links;
    for (auto& r : refs) {
      links += r.value;
      links.push_back(';');
    }
    return;  // TODO
    // throw parse_error("refs resolution max depth reached, yet unresolved links `{}`", links);
  }

  for (auto& [ref, parent, value] : refs) {
    // TODO вероятно всё же буст джсон так не работает
    // придётся с указателей уходить как-то
#if 0
    std::cout << "REF " << std::endl;
    pretty_print(std::cout, *ref);
    std::cout << "PARENT " << std::endl;
    pretty_print(std::cout, *parent);
    std::cout << "VALUE! " << std::endl;
    pretty_print(std::cout, v);
    on_scope_exit {
      std::cout << "REF AT END " << std::endl;
      pretty_print(std::cout, *ref);
      std::cout << "PARENT AT END " << std::endl;
      pretty_print(std::cout, *parent);
      std::cout << "VALUE! AT END" << std::endl;
      pretty_print(std::cout, v);
    };
#endif
    auto [replace, replace_path] = load_raw_refered_object(value, curfile, options);
    include_raw_references(replace, replace_path, depth - 1, options);
    assert(ref && parent);
    if (!replace.is_object())
      *ref = std::move(replace);
    else {
      if (parent->is_object()) {
        auto& parentobj = parent->as_object();
        // merge, не заменяем поля которые должны перекрыть то что в $ref
        // не запрещая при этом рекурсивные ссылки
        parentobj.erase("$ref");
        for (auto&& [rk, rv] : replace.as_object()) {
          if (!parentobj.contains(rk) || rk == "$ref")
            parentobj[rk] = std::move(rv);
        }
      } else {
        *ref = std::move(replace);
      }
    }
  }
}

boost::json::value resolve_raw_references(boost::json::value v, const svarg_options& options) {
  include_raw_references(v, options.input_specfile_path, options.ref_max_depth, options);
  return v;
}

}  // namespace svarg
