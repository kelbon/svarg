#pragma once

#include <filesystem>

#include "svarg/openapi_object.hpp"

/*
в этом файле openapi_object из спецификации openapi 3.1
с возможностью распарсить входной ямл или json в boost::json::value
*/

namespace svarg {

std::string yaml2json(std::string_view yaml);
std::string load_yaml_as_json(std::filesystem::path yaml);

struct svarg_options {
  // для любых ссылок глубина рекурсии ограничена
  // глубина 0 полностью запрещает ссылки
  size_t ref_max_depth = 10;
  // если встречается ссылка в виде URL переходить ли по ней (это может быть небезопасно, по умолчанию
  // выключено)
  // TODO под это ещё кеш надо сделать, чтобы лишний раз не ходить с запросами
  bool resolve_url_refs = false;
  // TODO unordered_map<string> known_formats?
};

// резолвит ссылки без #
boost::json::value preprocess_openapi_spec(boost::json::value json, const svarg_options& options);

// ожидается значение прошедшее preprocess_openapi_spec
openapi_object parse_spec(boost::json::value, const svarg_options& options);

// конвертирует из ямла в json
openapi_object parse_yaml_spec(std::string_view yaml, const svarg_options& options);
openapi_object parse_json_spec(std::string_view json, const svarg_options& options);

// может быть YAML (.yaml, .yml) или JSON (.json .jsonc .json5)
openapi_object parse_spec_file(std::filesystem::path path, const svarg_options& options);

// TODO в итоговом джсоне уже не должно быть всяких параметерс и тд прочей хуйни
// должен быть просто список типов, список операций (там прямо перечисленно какие типы в параметрах, их
// названия, где лежат, какие HTTP методы допустимы, тела и их контент типы и тд)
// TODO крч парсинг с валидацией, вероятно надо сразу выделить мой парсер генераторный. Надо его в отдельный
// проект. Причём токены он будет получать из чего угодно (чтобы можно было подменить эту часть)
}  // namespace svarg

///
/// ВАЛИДАЦИЯ
///

#include "svarg/utils/validation_specializations.hh"
