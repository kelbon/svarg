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

 public:
  const boost::json::value& load(fs::path path) {
    return load_mutable(std::move(path));
  }
  // нужно использовать осторожно, учитывая что значение лежит в кеше.
  // но иногда кеш нужно поменять
  boost::json::value& load_mutable(fs::path path);
};

}  // namespace svarg
