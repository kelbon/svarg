#pragma once

#include <boost/json/value.hpp>

#include <filesystem>
#include <unordered_map>

#include "svarg/utils/fn_ref.hpp"

namespace svarg {

std::string yaml2json(std::string_view yaml);
std::string load_yaml_as_json(std::filesystem::path yaml);

// передаёт в `fn` содержимое файла как json, даже если это yaml
void pass_as_json(std::filesystem::path path, fn_cref<void(std::string_view) const> fn);

// грузит и кеширует как json объекты yaml/json файлы
struct jsons_cache {
 private:
  std::unordered_map<std::filesystem::path, boost::json::value> cache;

 public:
  const boost::json::value& load(std::filesystem::path path);
};

}  // namespace svarg
