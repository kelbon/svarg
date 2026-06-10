#include "svarg/openapi.hpp"
#include <boost/json/parse.hpp>

namespace svarg {

// удаляет всякое лишнее, что по спецификации нужно игнорировать и тд и тп
void postprocess_openapi_object(openapi_object& obj);

openapi_object parse_spec(const boost::json::value& v, const svarg_options& options) {
  openapi_object obj;
  parse<openapi_object>{}(v, obj);
  postprocess_openapi_object(obj);
  return obj;
}

openapi_object parse_yaml_spec(std::string_view yaml, const svarg_options& options) {
  std::string json = yaml2json(yaml);
  return parse_json_spec(json, options);
}

openapi_object parse_json_spec(std::string_view json, const svarg_options& options) {
  boost::json::value v = boost::json::parse(json);
  v = resolve_raw_references(std::move(v), options);
  return parse_spec(std::move(v), options);
}

openapi_object parse_spec_file(std::filesystem::path path, svarg_options options) {
  options.input_specfile_path = path;
  openapi_object obj;
  pass_as_json(path, [&](std::string_view json) { obj = parse_json_spec(json, options); });
  return obj;
}

}  // namespace svarg
