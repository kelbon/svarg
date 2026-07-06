#include "svarg/frontend/openapi/openapi.hpp"
#include <boost/json/parse.hpp>

namespace svarg {

// удаляет всякое лишнее, что по спецификации нужно игнорировать и тд и тп
void postprocess_openapi_object(openapi_object& obj);

openapi_object parse_spec(const boost::json::value& v, const openapi_options& options) {
  openapi_object obj;
  from_json(v, obj);
  postprocess_openapi_object(obj);
  return obj;
}

openapi_object parse_yaml_spec(std::string_view yaml, fs::path specfile, const openapi_options& options) {
  std::string json = yaml2json(yaml);
  return parse_json_spec(json, std::move(specfile), options);
}

openapi_object parse_json_spec(std::string_view json, fs::path specfile, const openapi_options& options) {
  boost::json::value v = boost::json::parse(json);
  v = resolve_raw_references(std::move(v), std::move(specfile), options);
  return parse_spec(std::move(v), options);
}

openapi_object parse_spec_file(fs::path path, openapi_options options) {
  openapi_object obj;
  pass_as_json(path, [&](std::string_view json) { obj = parse_json_spec(json, std::move(path), options); });
  return obj;
}

}  // namespace svarg
