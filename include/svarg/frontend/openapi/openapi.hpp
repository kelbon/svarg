#pragma once

#include <filesystem>

#include "svarg/frontend/openapi/openapi_object.hpp"
#include "svarg/utils/jsons_cache.hpp"
#include "svarg/ir/options.hpp"

/*
в этом файле openapi_object из спецификации openapi 3.1
с возможностью распарсить входной ямл или json в boost::json::value
*/

namespace svarg {

struct openapi_options {
  // для любых ссылок глубина рекурсии ограничена
  // глубина 0 полностью запрещает ссылки
  size_t ref_max_depth = 10;
  // если false, то при достижении ref_max_depth будет не ошибка, а остановка на том что получилось
  bool ref_depth_is_a_fail = true;
  // если встречается ссылка в виде URL переходить ли по ней (это может быть небезопасно, по умолчанию
  // выключено)
  // TODO под это ещё кеш надо сделать, чтобы лишний раз не ходить с запросами
  bool resolve_url_refs = false;
  // TODO unordered_map<string> known_formats?
  // Зависят от порядка. При резолве ссылки берётся первый существующий файл
  // spec dir это тоже include dir и она имеет приоритет
  std::vector<fs::path> include_dirs = {};
  ir::ir_options ir_opts = {};
};

// резолвит ссылки без #
[[nodiscard]] boost::json::value resolve_raw_references(boost::json::value json, fs::path specfile,
                                                        const openapi_options& options,
                                                        jsons_cache* cache = nullptr);

// ожидается значение без ссылок не содержащих # (прошедшее resolve_raw_references)
openapi_object parse_spec(const boost::json::value&, const openapi_options& options);

// конвертирует из ямла в json
openapi_object parse_yaml_spec(std::string_view yaml, fs::path specfile, const openapi_options& options);
// `spec_path` используется для разрешения ссылок без #
openapi_object parse_json_spec(std::string_view json, fs::path specfile, const openapi_options& options);

// может быть YAML (.yaml, .yml) или JSON (.json .jsonc .json5)
// рекурсивные сырые(без #) ссылки не поддерживаются
// Например (в файле User.yaml)
//   recursiveProperty:
//    allOf:
//      - $ref: './User.yaml' <- рекурсия на файлах, не поддерживается.
// все ссылки без # работают как просто include того что написано файле с конвертацией в json value
// при ссылке внутри объекта на другой объект в $ref лежащие рядом поля переопределяют поля объекта по ссылке
openapi_object parse_spec_file(fs::path path, openapi_options options);

}  // namespace svarg

///
/// ВАЛИДАЦИЯ
///

#include "svarg/frontend/openapi/openapi_validation.hh"
