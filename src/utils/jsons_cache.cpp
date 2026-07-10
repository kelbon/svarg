#include "svarg/utils/jsons_cache.hpp"

#include <boost/json/parse.hpp>
#include <boost/json.hpp>

#include "svarg/utils/yamljson.hpp"

namespace svarg {

boost::json::value& jsons_cache::load_mutable(fs::path path_) {
  const fs::path path = fs::canonical(path_);
  auto it = cache.find(path);
  if (it != cache.end())
    return it->second;

  decltype(cache)::iterator res;
  pass_as_json(path, [&](std::string_view content) {
    boost::json::value json = boost::json::parse(content);
    if (preprocess)
      json = preprocess(path_, std::move(json));
    std::tie(res, std::ignore) = cache.try_emplace(std::move(path), std::move(json));
  });
  return res->second;
}

}  // namespace svarg
