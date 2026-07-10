#pragma once

#include <boost/json/value.hpp>

#include <filesystem>
#include <unordered_map>

#include "svarg/common_types.hpp"
#include "svarg/utils/fn_ref.hpp"

namespace svarg {

// грузит и кеширует как json объекты yaml/json файлы
struct jsons_cache {
 private:
  std::unordered_map<fs::path, boost::json::value> cache;
  // вызывается перед тем как положить json в кеш при загрузке
  move_only_fn<boost::json::value(const fs::path&, boost::json::value)> preprocess;

 public:
  // path передаётся в функцию точно такой же как и запрошенный в load/load_mutable
  void set_preprocessor(move_only_fn<boost::json::value(const fs::path&, boost::json::value)> pp) {
    preprocess = std::move(pp);
  }
  const boost::json::value& load(fs::path path) {
    return load_mutable(std::move(path));
  }
  // нужно использовать осторожно, учитывая что значение лежит в кеше.
  // но иногда кеш нужно поменять
  boost::json::value& load_mutable(fs::path path);
};

}  // namespace svarg
