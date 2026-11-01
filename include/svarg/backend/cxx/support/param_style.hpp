#pragma once

#include <array>
#include <bitset>
#include <string>
#include <string_view>

#include <boost/json/value.hpp>

#include "svarg/errors.hpp"
#include "svarg/utils/path_tools.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"
#include "svarg/backend/cxx/support/value_format.hpp"

// массивы и объекты в параметрах path/query/header/cookie (OpenAPI style по умолчанию + explode)
// https://spec.openapis.org/oas/v3.1.0#style-examples
//
// Значение параметра бывает простым (строка, число, bool, енам), массивом простых значений или объектом с
// простыми свойствами. Остальное (вложенные массивы, комбинаторы) пишется JSON, как и раньше. Параметр с
// `content:` (fields[I].encoding) кодируется своим медиа типом.
//
//            массив                 объект, explode           объект без explode
//  path      1,2                    R=100,G=200               R,100,G,200
//  header    1,2                    R=100,G=200               R,100,G,200
//  query     ids=1&ids=2 (explode)  R=100&G=200               color=R,100,G,200
//            ids=1,2 (без explode)
//  cookie    как query, через "; "
//
// explode по умолчанию у query и cookie, явно - fields[I].explode в структуре параметров.
// Элементы кодируются по отдельности, разделители остаются как есть: в query "a%2Cb" - один элемент "a,b".
// В хедерах и куках percent-encoding нет, поэтому там запятая в элементе неотличима от разделителя

namespace svarg::cxx_support {

enum struct param_location_e { path, query, header, cookie };

template <param_location_e L, typename T, size_t I>
consteval bool explode_of() {
  if constexpr (has_named_fields<T>) {
    if constexpr (constexpr std::optional<bool> e = get_field_info<I, T>().explode; e.has_value())
      return *e;
  }
  return L == param_location_e::query || L == param_location_e::cookie;
}

template <typename T, size_t I>
consteval std::string_view content_encoding_of() {
  return get_field_encoding<I, T>();
}

enum struct param_kind_e { plain, array, object, other };

namespace noexport {

template <typename V>
struct array_items_type {
  using type = void;
};

template <typename X, typename A>
struct array_items_type<std::vector<X, A>> {
  using type = std::vector<X, A>;
};

template <typename V>
  requires(an_array<V>)
struct array_items_type<V> {
  using type = std::remove_cvref_t<decltype(std::declval<V&>()._implfield)>;
};

template <typename V>
consteval bool all_fields_plain() {
  return []<size_t... Is>(std::index_sequence<Is...>) {
    return (is_plain_value<std::remove_cvref_t<decltype(get_field<Is>(std::declval<V&>()))>> && ...);
  }(std::make_index_sequence<fields_count<V>()>{});
}

}  // namespace noexport

template <typename V>
consteval param_kind_e param_kind_of() {
  using U = svarg::noexport::deoptional_type_t<V>;
  if constexpr (is_plain_value<U>) {
    return param_kind_e::plain;
  } else if constexpr (!std::is_void_v<typename noexport::array_items_type<U>::type>) {
    using items_t = typename noexport::array_items_type<U>::type;
    return is_plain_value<typename items_t::value_type> ? param_kind_e::array : param_kind_e::other;
  } else if constexpr (an_object<U>) {
    if constexpr (!has_additional_properties<U> && noexport::all_fields_plain<U>())
      return param_kind_e::object;
    else
      return param_kind_e::other;
  } else {
    return param_kind_e::other;
  }
}

namespace noexport {

// поле параметра без optional: создаёт значение, если его нет
template <typename V>
auto& emplace_value(V& v) {
  if constexpr (requires {
                  v.has_value();
                  v.emplace();
                })
    return v.has_value() ? *v : v.emplace();
  else
    return v;
}

template <typename V>
const auto& present_value(const V& v) {
  if constexpr (requires { v.has_value(); })
    return *v;
  else
    return v;
}

// элементы массива, уже закодированные `enc`
template <typename V>
[[nodiscard]] std::vector<std::string> array_items(const V& v, auto&& enc) {
  const auto& items = [&]() -> const auto& {
    if constexpr (an_array<V>)
      return get_special_field(v);
    else
      return v;
  }();
  std::vector<std::string> r;
  for (const auto& x : items)
    r.push_back(enc(serialize_nonbody_into(x)));
  return r;
}

// пары (имя, значение) присутствующих свойств объекта, уже закодированные `enc`
template <typename V>
[[nodiscard]] std::vector<std::pair<std::string, std::string>> object_properties(const V& v, auto&& enc) {
  std::vector<std::pair<std::string, std::string>> r;
  FOR_EACH_FIELD_INDEX_OF(V) {
    if (is_field_present<I>(v))
      r.emplace_back(enc(get_field_name<I, V>()), enc(serialize_nonbody_into(get_field<I>(v))));
  };
  return r;
}

[[nodiscard]] inline std::string join(const auto& parts, std::string_view sep, auto&& to_str) {
  std::string r;
  bool first = true;
  for (const auto& p : parts) {
    if (!first)
      r += sep;
    first = false;
    r += to_str(p);
  }
  return r;
}

// "a,b,c" -> {a, b, c}, пустая строка - пустой массив
[[nodiscard]] inline std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> r;
  if (s.empty())
    return r;
  for (;;) {
    size_t pos = s.find(sep);
    r.push_back(s.substr(0, pos));
    if (pos == s.npos)
      return r;
    s.remove_prefix(pos + 1);
  }
}

// значения разбираются по своим типам и собираются в JSON, затем try_from_json проверяет массив или
// объект целиком (minItems, обязательные свойства...)
template <typename V>
[[nodiscard]] error_code add_array_item(std::string_view value, boost::json::value& acc) {
  typename array_items_type<V>::type::value_type x;
  if (error_code ec = parse_nonbody_from(value, x))
    return ec;
  if (acc.is_null())
    acc = boost::json::array();
  acc.as_array().push_back(to_json(x));
  return {};
}

template <typename V>
[[nodiscard]] error_code add_object_property(std::string_view key, std::string_view value,
                                             boost::json::value& acc) {
  std::optional<size_t> index = get_field_index<V>(key);
  if (!index)
    return parse_error("unexpected property `{}` of `{}`", key, type_name<V>());
  if (acc.is_null())
    acc = boost::json::object();
  return visit_field_index<fields_count<V>()>(
      [&]<size_t I>() -> error_code {
        std::remove_cvref_t<decltype(get_field<I>(std::declval<V&>()))> x;
        if (error_code ec = parse_nonbody_from(value, x))
          return ec;
        acc.as_object()[key] = to_json(x);
        return {};
      },
      *index);
}

// элементы без explode: "1,2" или "R,100,G,200"
template <typename V>
[[nodiscard]] error_code add_joined(std::string_view raw, auto&& dec, boost::json::value& acc) {
  std::vector<std::string_view> parts = split(raw, ',');
  std::string buf, kbuf;
  if constexpr (param_kind_of<V>() == param_kind_e::array) {
    for (std::string_view p : parts)
      if (error_code ec = add_array_item<V>(dec(p, buf), acc))
        return ec;
    if (acc.is_null())
      acc = boost::json::array();  // пустой массив
  } else {
    if (parts.size() % 2 != 0)
      return parse_error("odd number of key/value parts in `{}`", raw);
    for (size_t i = 0; i < parts.size(); i += 2) {
      std::string key(dec(parts[i], kbuf));
      if (error_code ec = add_object_property<V>(key, dec(parts[i + 1], buf), acc))
        return ec;
    }
  }
  return {};
}

}  // namespace noexport

// ОДНА СТРОКА: path, header

// поле I структуры параметров T. `separator` - разделитель из шаблона пути после параметра
template <param_location_e L, size_t I, typename T>
[[nodiscard]] std::string serialize_param_field(const T& params, std::string_view separator = {}) {
  static_assert(L == param_location_e::path || L == param_location_e::header);
  using V = svarg::noexport::deoptional_type_t<std::remove_cvref_t<decltype(get_field<I>(params))>>;
  constexpr param_kind_e kind = param_kind_of<V>();
  auto enc = [&](std::string_view s) {
    if constexpr (L == param_location_e::path) {
      // ',' и '=' - разделители элементов, хоть и допустимы в пути
      std::string extra(separator);
      if (kind == param_kind_e::array || kind == param_kind_e::object)
        extra += ",=";
      std::string out;
      percent_encode_path_value(s, extra, std::back_inserter(out));
      return out;
    } else {
      return std::string(s);
    }
  };
  if constexpr (!content_encoding_of<T, I>().empty()) {
    return enc(serialize_nonbody_field<I>(params));
  } else {
    const V& v = noexport::present_value(get_field<I>(params));
    if constexpr (kind == param_kind_e::array) {
      return noexport::join(noexport::array_items(v, enc), ",", std::identity{});
    } else if constexpr (kind == param_kind_e::object) {
      constexpr bool explode = explode_of<L, T, I>();
      return noexport::join(noexport::object_properties(v, enc), ",", [](const auto& kv) {
        return std::format("{}{}{}", kv.first, explode ? "=" : ",", kv.second);
      });
    } else {
      return enc(serialize_nonbody_into(v));
    }
  }
}

template <param_location_e L, size_t I, typename T>
[[nodiscard]] error_code parse_param_field(std::string_view raw, T& params) {
  static_assert(L == param_location_e::path || L == param_location_e::header);
  using V = svarg::noexport::deoptional_type_t<std::remove_cvref_t<decltype(get_field<I>(params))>>;
  constexpr param_kind_e kind = param_kind_of<V>();
  auto dec = [](std::string_view s, std::string& buf) -> std::string_view {
    if constexpr (L == param_location_e::path)
      return percent_decode_path_value(s, buf);
    else
      return s;
  };
  std::string buf;
  if constexpr (!content_encoding_of<T, I>().empty() || kind == param_kind_e::plain ||
                kind == param_kind_e::other) {
    return parse_nonbody_field<I>(dec(raw, buf), params);
  } else {
    boost::json::value acc;
    error_code ec;
    if constexpr (kind == param_kind_e::object && explode_of<L, T, I>()) {
      // R=100,G=200
      for (std::string_view part : noexport::split(raw, ',')) {
        size_t eq = part.find('=');
        if (eq == part.npos)
          return parse_error("expected `key=value` in `{}`", part);
        std::string key(dec(part.substr(0, eq), buf));
        std::string kbuf;
        if ((ec = noexport::add_object_property<V>(key, dec(part.substr(eq + 1), kbuf), acc)))
          return ec;
      }
      if (acc.is_null())
        acc = boost::json::object();
    } else {
      if ((ec = noexport::add_joined<V>(raw, dec, acc)))
        return ec;
      if (acc.is_null())
        acc = boost::json::object();
    }
    return try_from_json(acc, noexport::emplace_value(get_field<I>(params)));
  }
}

// ПАРЫ: query, cookie

// вызывает emit(ключ, значение) для каждой пары поля I, ключ и значение уже закодированы
template <param_location_e L, size_t I, typename T>
void serialize_param_pairs(const T& params, auto&& emit) {
  static_assert(L == param_location_e::query || L == param_location_e::cookie);
  auto enc = [](std::string_view s) {
    if constexpr (L == param_location_e::query) {
      std::string out;
      query_encode(s, std::back_inserter(out));
      return out;
    } else {
      return std::string(s);
    }
  };
  std::string name = enc(get_field_name<I, T>());
  if constexpr (!content_encoding_of<T, I>().empty()) {
    emit(name, enc(serialize_nonbody_field<I>(params)));
  } else {
    using V = svarg::noexport::deoptional_type_t<std::remove_cvref_t<decltype(get_field<I>(params))>>;
    constexpr param_kind_e kind = param_kind_of<V>();
    constexpr bool explode = explode_of<L, T, I>();
    const V& v = noexport::present_value(get_field<I>(params));
    if constexpr (kind == param_kind_e::array) {
      std::vector<std::string> items = noexport::array_items(v, enc);
      if constexpr (explode) {
        for (auto& item : items)
          emit(name, item);
      } else {
        emit(name, noexport::join(items, ",", std::identity{}));
      }
    } else if constexpr (kind == param_kind_e::object) {
      auto props = noexport::object_properties(v, enc);
      if constexpr (explode) {
        for (auto& [k, val] : props)
          emit(k, val);
      } else {
        emit(name, noexport::join(props, ",", [](const auto& kv) { return kv.first + "," + kv.second; }));
      }
    } else {
      emit(name, enc(serialize_nonbody_into(v)));
    }
  }
}

// разбор пар в структуру параметров T: on_pair для каждой пары (ключ и значение как пришли, без
// декодирования), затем finish. Массивы и объекты накапливаются и проверяются целиком в finish.
// Неизвестные ключи игнорируются, если это не свойство объекта с explode
template <param_location_e L, typename T>
struct param_pairs_parser {
  T& out;
  std::bitset<fields_count<T>()> unparsed = get_required_bits_mask<T>();
  // накопленные значения массивов и объектов, null - не встречались
  std::array<boost::json::value, fields_count<T>()> acc{};
  std::string kbuf, vbuf;

  static std::string_view dec(std::string_view s, std::string& buf) {
    if constexpr (L == param_location_e::query) {
      if (!s.contains('%') && !s.contains('+'))
        return s;
      buf.clear();
      query_decode</*WwwUrlencoded=*/true>(s, std::back_inserter(buf));
      return buf;
    } else {
      return s;
    }
  }

  [[nodiscard]] error_code on_pair(std::string_view raw_key, std::string_view raw_value) {
    if constexpr (fields_count<T>() == 0) {
      return {};
    } else {
      std::string key(dec(raw_key, kbuf));
      if (std::optional<size_t> i = get_field_index<T>(key)) {
        unparsed.set(*i, false);
        return visit_field_index<fields_count<T>()>([&]<size_t I>() { return on_field<I>(raw_value); }, *i);
      }
      // свойство объекта с explode: первый объект, у которого есть такое свойство
      error_code ec;
      bool found = false;
      FOR_EACH_FIELD_INDEX_OF(T) {
        using V = field_t<I>;
        if constexpr (param_kind_of<V>() == param_kind_e::object && explode_of<L, T, I>() &&
                      content_encoding_of<T, I>().empty()) {
          if (found || !get_field_index<V>(key))
            return;
          found = true;
          unparsed.set(I, false);
          ec = noexport::add_object_property<V>(key, dec(raw_value, vbuf), acc[I]);
        }
      };
      return ec;
    }
  }

  [[nodiscard]] error_code finish() {
    if (unparsed.any())
      return parse_error("not all required {} parameters are present, unparsed fields: {}",
                         L == param_location_e::query ? "query" : "cookie", unparsed.to_string());
    error_code ec;
    FOR_EACH_FIELD_INDEX_OF(T) {
      if (ec || acc[I].is_null())
        return;
      ec = try_from_json(acc[I], noexport::emplace_value(get_field<I>(out)));
    };
    return ec;
  }

 private:
  template <size_t I>
  using field_t =
      svarg::noexport::deoptional_type_t<std::remove_cvref_t<decltype(get_field<I>(std::declval<T&>()))>>;

  template <size_t I>
  [[nodiscard]] error_code on_field(std::string_view raw_value) {
    using V = field_t<I>;
    constexpr param_kind_e kind = param_kind_of<V>();
    if constexpr (!content_encoding_of<T, I>().empty() || kind == param_kind_e::plain ||
                  kind == param_kind_e::other) {
      return parse_nonbody_field<I>(dec(raw_value, vbuf), out);
    } else if constexpr (kind == param_kind_e::array && explode_of<L, T, I>()) {
      return noexport::add_array_item<V>(dec(raw_value, vbuf), acc[I]);
    } else {
      // элементы разделены до декодирования: %2C внутри элемента не разделитель
      return noexport::add_joined<V>(raw_value, dec, acc[I]);
    }
  }
};

}  // namespace svarg::cxx_support
