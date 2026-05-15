#include "svarg/openapi.hpp"

#include <boost/iostreams/device/mapped_file.hpp>
#include <ryml.hpp>
#include <boost/json.hpp>

#include "svarg/utils/parse_json.hpp"

namespace svarg {

std::string yaml2json(std::string_view yaml) {
  ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(yaml.data(), yaml.size()));
  return ryml::emitrs_json<std::string>(tree);
}

inline std::string load_yaml_as_json(std::filesystem::path yaml) {
  boost::iostreams::mapped_file_source file("spec.yaml");
  return yaml2json(std::string_view(file.data(), file.size()));
}

openapi_object parse_yaml_spec(std::string_view yaml, const svarg_options& options) {
  std::string json = yaml2json(yaml);
  return parse_json_spec(json, options);
}

openapi_object parse_spec(std::filesystem::path path, const svarg_options& options) {
  std::filesystem::path ext = path.extension();
  if (ext == ".yaml" || ext == ".yml") {
    std::string json = load_yaml_as_json(std::move(path));
    return parse_json_spec(json, options);
  } else if (ext == ".json" || ext == ".jsonc" || ext == "json5") {
    boost::iostreams::mapped_file_source file("spec.yaml");
    return parse_json_spec(std::string_view(file.data(), file.size()), options);
  } else {
    throw std::invalid_argument("invalid spec extensions, allowed extensions .json and .yaml");
  }
}

// TODO в общем вероятно надо принимать все спеки за раз, парсить их и линковать всё в одно
// А потом применять шаблоны типов, запросов и тд и раскладывать по папкам
// TODO функцию парсинга ссылки openapi. По сути там всё просто - <путь до файла> # <json pointer>
// надо с каждой спекой рядом положить где она лежит, чтобы относительные пути резолвить относительно её пути
// и всё
// и учесть что ссылки могут резолвиться в ссылку и тд. Желательно тут детектить цикл
// причём ИНОГДА ЭТО ВАЛИДНО. Т.е. Tree у которого left это ссылка на Tree это валидно!
// плюс нужно json поинтер предобрабатывать если там есть ~0 ~1 (спецсимволы для замены ~ и /)
// А для URL надо видимо опционально включать их обработку, чтобы запросы абы куда не делать
// но в целом это тупо GET, т.е. вижу http:// делаю запрос без ссл, https:// - ssl
// и там видимо должен быть документ yaml/json и его нужно распарсить
// А ещё # может не встретится. Тогда ref работает типо как include, весь документ берёт
// такие ссылки видимо надо на этапе препроцессинга подставлять. Типа сразу - видишь $ref, там нет # -
// подставляешь документ целиком. Причём там внутри может быть ещё одна такая же ссылка, так что это
// рекурсивная хрень и да, нужно следить чтобы он не подставил сам себя или что-то рекурсивное, иначе будет
// бесконечный пиздец. Проще всего просто ограничить этот процесс каким-то максимальным числом вложенных
// ссылок и регулировать его (иначе можно такого наколупать, что невозможно алгортимически понять что
// инклудишь уже пиздец)

openapi_object parse_spec(boost::json::value v, const svarg_options& options) {
  openapi_object obj;
  parse<openapi_object>{}(v, obj);
  return obj;
}

openapi_object parse_json_spec(std::string_view json, const svarg_options& options) {
  boost::json::value v = boost::json::parse(json);
  v = preprocess_openapi_spec(std::move(v), options);
  return parse_spec(std::move(v), options);
}

}  // namespace svarg
