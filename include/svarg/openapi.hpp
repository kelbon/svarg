#pragma once

#include <filesystem>

#include "svarg/openapi_object.hpp"
#include "svarg/utils/jsons_cache.hpp"

/*
в этом файле openapi_object из спецификации openapi 3.1
с возможностью распарсить входной ямл или json в boost::json::value
*/

namespace svarg {

struct svarg_options {
  // для любых ссылок глубина рекурсии ограничена
  // глубина 0 полностью запрещает ссылки
  size_t ref_max_depth = 10;
  // если false, то при достижении ref_max_depth будет не ошибка, а остановка на том что получилось
  bool ref_depth_is_a_fail = true;
  // если встречается ссылка в виде URL переходить ли по ней (это может быть небезопасно, по умолчанию
  // выключено)
  // TODO под это ещё кеш надо сделать, чтобы лишний раз не ходить с запросами
  bool resolve_url_refs = false;
  // при подаче любого файла на вход все пути всё равно считаются относительно него, так что достаточно одного
  // любого пути к спецификации для сбора всех нужных
  std::filesystem::path input_specfile_path;
  // TODO unordered_map<string> known_formats?
};

// резолвит ссылки без #
[[nodiscard]] boost::json::value resolve_raw_references(boost::json::value json, const svarg_options& options,
                                                        jsons_cache* cache = nullptr);

// ожидается значение без ссылок не содержащих # (прошедшее resolve_raw_references)
openapi_object parse_spec(boost::json::value, const svarg_options& options);

// конвертирует из ямла в json
openapi_object parse_yaml_spec(std::string_view yaml, const svarg_options& options);
// `spec_path` используется для разрешения ссылок без #
openapi_object parse_json_spec(std::string_view json, const svarg_options& options);

// может быть YAML (.yaml, .yml) или JSON (.json .jsonc .json5)
// `path` выставляется в options.input_specfile_path
// рекурсивные сырые(без #) ссылки не поддерживаются
// Например (в файле User.yaml)
//   recursiveProperty:
//    allOf:
//      - $ref: './User.yaml' <- рекурсия на файлах, не поддерживается.
// все ссылки без # работают как просто include того что написано файле с конвертацией в json value
// при ссылке внутри объекта на другой объект в $ref лежащие рядом поля переопределяют поля объекта по ссылке
openapi_object parse_spec_file(std::filesystem::path path, svarg_options options);

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
