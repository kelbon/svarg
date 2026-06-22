#include "svarg/utils/jsons_cache.hpp"

#include <boost/json/parse.hpp>
#include <boost/json.hpp>

#include "svarg/utils/yamljson.hpp"

namespace svarg {

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
