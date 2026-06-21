#include "svarg/utils/json_compare.hpp"
#include <compare>

namespace svarg {

namespace json = boost::json;

std::strong_ordering json_compare(const json::value& a, const json::value& b) {
  // надеемся, что всё таки сохранили equality
  // if (a == b) return std::strong_ordering::equal
  if (a.kind() != b.kind())
    return std::to_underlying(a.kind()) <=> std::to_underlying(b.kind());

  switch (a.kind()) {
    case json::kind::null:
      return std::strong_ordering::equal;
    case json::kind::bool_:
      return a.get_bool() <=> b.get_bool();
    case json::kind::int64:
      return a.get_int64() <=> b.get_int64();
    case json::kind::uint64:
      return a.get_uint64() <=> b.get_uint64();
    case json::kind::double_:
      return std::strong_order(a.get_double(), b.get_double());
    case json::kind::string:
      return std::string_view(a.get_string()) <=> std::string_view(b.get_string());
    case json::kind::array: {
      const auto& aa = a.get_array();
      const auto& ab = b.get_array();
      auto n = std::min(aa.size(), ab.size());
      for (size_t i = 0; i < n; ++i) {
        auto c = json_compare(aa[i], ab[i]);
        if (c != std::strong_ordering::equal)
          return c;
      }
      return aa.size() <=> ab.size();
    }
    case json::kind::object: {
      const auto& oa = a.get_object();
      const auto& ob = b.get_object();

      // ))))
      std::vector<std::string_view> ka, kb;
      for (auto& kv : oa)
        ka.push_back(kv.key());
      for (auto& kv : ob)
        kb.push_back(kv.key());
      std::sort(ka.begin(), ka.end());
      std::sort(kb.begin(), kb.end());
      auto n = std::min(ka.size(), kb.size());
      for (size_t i = 0; i < n; ++i) {
        auto c = ka[i] <=> kb[i];
        if (c != std::strong_ordering::equal)
          return c;
      }
      if (auto c = ka.size() <=> kb.size(); c != std::strong_ordering::equal)
        return c;
      // все ключи оказались равны и размеры не равны, сравниваем значения
      for (auto& k : ka) {
        auto c = json_compare(*oa.if_contains(k), *ob.if_contains(k));
        if (c != std::strong_ordering::equal)
          return c;
      }
      return std::strong_ordering::equal;
    }
  }
  return std::strong_ordering::equal;
}

}  // namespace svarg
