#include "svarg/backend/cxx/cxx_ident.hpp"
#include "svarg/backend/cxx/render.hpp"

namespace svarg::cxx {

static std::string make_cxx_ident(std::string_view str, const render_options& options) {
  return options.simple_cxx_identifiers ? to_cxx_ident_simple(str) : to_cxx_ident(str);
}
// TODO похоже для корретной генерации надо понимать где мы (в каком неймспейсе генерируем)
std::string render_type(const ir::schema_type& type, const std::unordered_set<ir::schema>& schemas,
                        std::vector<ir::schema_ref>& mentioned, const render_options& options) {
  // TODO!
  /*
если schema.type фундаментальный тип - просто string/null и тд
если schema.type anyof - делаем f"anyof<{render_type(schemaof(schema.type))}>"
одинаково для arrayof/allof/oneof/anyof/not
если schema.type не такая простая, а объект, то берём jsonptr где она лежит, превращаем в ::name::space , при
этом вычисляя относительно нынешнего неймспейса, чтобы покороче было. Это по сути относительный путь мы в
a/b/c, тип в /b/c - результат b::c мы в a/b/c, тип в d/e - результат ::d::e мы в a/b/c тип в a/b - результат
просто пустота, можно так писать мы в a/b/c, тип в b - пустота (хотя такого быть не должно) мы в a тип в a/b/c
- результат b::c

Суть такова:
если a.contains(b) - пустота
если b.starts_with(a) - результат b.substr(a.size())
else :: + b
*/
  // visit_one(type.data, Foos && foos...);
  return "";
}

}  // namespace svarg::cxx
