#include "svarg/utils/jsons_cache.hpp"

#include <boost/json/parse.hpp>
#include <boost/json.hpp>

#include <boost/iostreams/device/mapped_file.hpp>
#include <ryml.hpp>
// не спрашивайте... ryml гениален. ПОРЯДОК ХЕДЕРОВ ВАЖЕН
#include <ryml_std.hpp>

namespace svarg {

void pass_as_json(fs::path path, fn_cref<void(std::string_view) const> fn) {
  fs::path ext = path.extension();
  if (ext == ".yaml" || ext == ".yml") {
    std::string json = load_yaml_as_json(path);
    return fn(std::string_view(json));
  } else if (ext == ".json" || ext == ".jsonc" || ext == "json5") {
    boost::iostreams::mapped_file_source file(path.string());
    return fn(std::string_view(file.data(), file.size()));
  } else {
    throw std::invalid_argument("invalid spec extensions, allowed extensions .json and .yaml");
  }
}

std::string yaml2json(std::string_view yaml) {
  ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(yaml.data(), yaml.size()));
  return ryml::emitrs_json<std::string>(tree);
}

inline std::string load_yaml_as_json(fs::path yaml) {
  boost::iostreams::mapped_file_source file(yaml.string());
  return yaml2json(std::string_view(file.data(), file.size()));
}

boost::json::value& jsons_cache::load_mutable(fs::path path) {
  path = fs::canonical(path);
  auto it = cache.find(path);
  if (it != cache.end())
    return it->second;
  decltype(cache)::iterator res;

  pass_as_json(path, [&](std::string_view content) {
    auto [it, _] = cache.try_emplace(std::move(path), boost::json::parse(content));
    res = std::move(it);
  });
  return res->second;
}

}  // namespace svarg
