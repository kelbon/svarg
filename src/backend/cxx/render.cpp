#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/render.hpp"

namespace svarg::cxx {

static std::string make_cxx_ident(std::string_view str, const render_options& options) {
  return options.simple_cxx_identifiers ? to_cxx_ident_simple(str) : to_cxx_ident(str);
}

std::string render_type(const ir::schema_type& type, const std::unordered_set<ir::schema>& schemas,
                        std::vector<ir::schema_ref>& mentioned, const render_options& options) {
  // TODO
  // visit_one(type.data, Foos && foos...);
  return "";
}

}  // namespace svarg::cxx
