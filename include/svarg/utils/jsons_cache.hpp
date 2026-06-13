#pragma once

#include <boost/json/value.hpp>

#include <filesystem>
#include <unordered_map>

#include "svarg/common_types.hpp"
#include "svarg/utils/fn_ref.hpp"

namespace svarg {

// можно подавать и json тоже, внезапно.
// так как json это подмножество yaml
std::string yaml2json(std::string_view yaml);
std::string load_yaml_as_json(fs::path yaml);

// передаёт в `fn` содержимое файла как json, даже если это yaml
void pass_as_json(fs::path path, fn_cref<void(std::string_view) const> fn);

// грузит и кеширует как json объекты yaml/json файлы
struct jsons_cache {
 private:
  std::unordered_map<fs::path, boost::json::value> cache;

 public:
  const boost::json::value& load(fs::path path) {
    return load_mutable(std::move(path));
  }
  // нужно использовать осторожно, учитывая что значение лежит в кеше.
  // но иногда кеш нужно поменять
  boost::json::value& load_mutable(fs::path path);
};

}  // namespace svarg
