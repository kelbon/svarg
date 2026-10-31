#pragma once

#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <metel/box.hpp>

#include "svarg/errors.hpp"
#include "svarg/to_json_value.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/oneof.hpp"
#include "svarg/backend/cxx/support/anyof.hpp"
#include "svarg/backend/cxx/support/utility_for_generated.hpp"

// ограничения JSON Schema (ir::constraints_schema), проверяются по уже разобранному значению
// генерируются в тип-владелец как `static constexpr std::array<constraints_t, N> constraints`

namespace svarg::cxx_support {

// граница для чисел. Целые сравниваются точно, остальное через double
using bound_t = std::variant<int64_t, uint64_t, double>;

struct constraints_t {
  // строки, длина в символах Unicode
  std::optional<uint64_t> min_length = std::nullopt;
  std::optional<uint64_t> max_length = std::nullopt;
  // ECMA-262, пустая строка - нет ограничения
  std::string_view pattern = {};

  // числа
  std::optional<bound_t> minimum = std::nullopt;
  std::optional<bound_t> maximum = std::nullopt;
  std::optional<bound_t> exclusive_minimum = std::nullopt;
  std::optional<bound_t> exclusive_maximum = std::nullopt;
  std::optional<bound_t> multiple_of = std::nullopt;

  // массивы
  std::optional<uint64_t> min_items = std::nullopt;
  std::optional<uint64_t> max_items = std::nullopt;
  bool unique_items = false;

  // объекты
  std::optional<uint64_t> min_properties = std::nullopt;
  std::optional<uint64_t> max_properties = std::nullopt;
};

template <typename T>
concept has_constraints = requires { std::decay_t<T>::constraints; };

namespace noexport {

// количество символов Unicode в UTF-8 строке (без проверки корректности UTF-8)
[[nodiscard]] constexpr size_t utf8_length(std::string_view s) noexcept {
  size_t n = 0;
  for (unsigned char c : s)
    n += (c & 0xC0) != 0x80;
  return n;
}

[[nodiscard]] inline std::partial_ordering compare_numbers(bound_t a, bound_t b) noexcept {
  return std::visit(
      []<typename A, typename B>(A x, B y) -> std::partial_ordering {
        if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
          if (std::cmp_less(x, y))
            return std::partial_ordering::less;
          if (std::cmp_greater(x, y))
            return std::partial_ordering::greater;
          return std::partial_ordering::equivalent;
        } else {
          return double(x) <=> double(y);
        }
      },
      a, b);
}

[[nodiscard]] inline bool is_multiple_of(bound_t v, bound_t m) noexcept {
  return std::visit(
      []<typename V, typename M>(V x, M y) -> bool {
        if constexpr (std::is_integral_v<V> && std::is_integral_v<M>) {
          // модули, чтобы не смешивать знаковые и беззнаковые
          auto abs_u = [](auto i) -> uint64_t {
            if constexpr (std::is_signed_v<decltype(i)>)
              return i < 0 ? uint64_t(-(i + 1)) + 1 : uint64_t(i);
            else
              return uint64_t(i);
          };
          uint64_t d = abs_u(y);
          return d != 0 && abs_u(x) % d == 0;
        } else {
          double q = double(x) / double(y);
          if (!std::isfinite(q))
            return false;
          return std::abs(q - std::round(q)) <= 1e-9 * std::max(1.0, std::abs(q));
        }
      },
      v, m);
}

[[nodiscard]] inline std::string bound_str(bound_t b) {
  return std::visit([](auto x) { return std::format("{}", x); }, b);
}

template <typename T>
constexpr bool is_arrayof = false;

template <typename T>
constexpr bool is_arrayof<arrayof<T>> = true;

// std::optional / metel::box
template <typename T>
constexpr bool is_optional_like = requires(const T& v) {
  v.has_value();
  *v;
};

template <typename T>
constexpr bool is_number_value = (std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
                                 std::is_floating_point_v<T> || std::is_same_v<T, number>;

template <typename T>
[[nodiscard]] bound_t to_bound(const T& v) noexcept {
  if constexpr (std::is_same_v<T, number>)
    return v.data;
  else if constexpr (std::is_floating_point_v<T>)
    return double(v);
  else if constexpr (std::is_signed_v<T>)
    return int64_t(v);
  else
    return uint64_t(v);
}

// регулярное выражение компилируется один раз на ограничение
template <typename Owner, size_t I>
[[nodiscard]] const std::regex& pattern_regex() {
  static const std::regex re(std::string(Owner::constraints[I].pattern), std::regex::ECMAScript);
  return re;
}

template <typename Owner, size_t I>
[[nodiscard]] error_code check_string(std::string_view s) {
  constexpr const constraints_t& c = Owner::constraints[I];
  if constexpr (c.min_length || c.max_length) {
    size_t len = utf8_length(s);
    if (c.min_length && len < *c.min_length)
      return validate_error("string length {} is less than minLength {}", len, *c.min_length);
    if (c.max_length && len > *c.max_length)
      return validate_error("string length {} is greater than maxLength {}", len, *c.max_length);
  }
  if constexpr (!c.pattern.empty()) {
    if (!std::regex_search(s.begin(), s.end(), pattern_regex<Owner, I>()))
      return validate_error("string `{}` does not match pattern `{}`", s, c.pattern);
  }
  return {};
}

template <typename Owner, size_t I>
[[nodiscard]] error_code check_number(bound_t v) {
  constexpr const constraints_t& c = Owner::constraints[I];
  using std::is_lt, std::is_gt, std::is_lteq, std::is_gteq;
  if (c.minimum && is_lt(compare_numbers(v, *c.minimum)))
    return validate_error("{} is less than minimum {}", bound_str(v), bound_str(*c.minimum));
  if (c.maximum && is_gt(compare_numbers(v, *c.maximum)))
    return validate_error("{} is greater than maximum {}", bound_str(v), bound_str(*c.maximum));
  if (c.exclusive_minimum && is_lteq(compare_numbers(v, *c.exclusive_minimum)))
    return validate_error("{} is not greater than exclusiveMinimum {}", bound_str(v),
                          bound_str(*c.exclusive_minimum));
  if (c.exclusive_maximum && is_gteq(compare_numbers(v, *c.exclusive_maximum)))
    return validate_error("{} is not less than exclusiveMaximum {}", bound_str(v),
                          bound_str(*c.exclusive_maximum));
  if (c.multiple_of && !is_multiple_of(v, *c.multiple_of))
    return validate_error("{} is not a multiple of {}", bound_str(v), bound_str(*c.multiple_of));
  return {};
}

template <typename Owner, size_t I, typename T>
[[nodiscard]] error_code check_array(const arrayof<T>& arr) {
  constexpr const constraints_t& c = Owner::constraints[I];
  if (c.min_items && arr.size() < *c.min_items)
    return validate_error("array size {} is less than minItems {}", arr.size(), *c.min_items);
  if (c.max_items && arr.size() > *c.max_items)
    return validate_error("array size {} is greater than maxItems {}", arr.size(), *c.max_items);
  if constexpr (c.unique_items) {
    // сравнение как JSON значений, генерируемые типы не обязаны иметь operator==
    std::vector<boost::json::value> seen;
    seen.reserve(arr.size());
    for (auto& x : arr) {
      boost::json::value j = to_json(x);
      if (std::ranges::contains(seen, j))
        return validate_error("array items are not unique, duplicate: `{}`", boost::json::serialize(j));
      seen.push_back(std::move(j));
    }
  }
  return {};
}

template <typename Owner, size_t I>
[[nodiscard]] error_code check_properties_count(size_t count) {
  constexpr const constraints_t& c = Owner::constraints[I];
  if (c.min_properties && count < *c.min_properties)
    return validate_error("object has {} properties, less than minProperties {}", count, *c.min_properties);
  if (c.max_properties && count > *c.max_properties)
    return validate_error("object has {} properties, greater than maxProperties {}", count,
                          *c.max_properties);
  return {};
}

// количество присутствующих свойств объекта, включая дополнительные
template <an_object T>
[[nodiscard]] size_t properties_count(const T& obj) {
  size_t n = 0;
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    ((n += is_field_present<Is>(obj)), ...);
  }(std::make_index_sequence<fields_count<T>()>{});
  if constexpr (has_additional_properties<T>)
    n += get_additional_properties(obj).size();
  return n;
}

// ограничение применяется только к значению подходящего типа, остальные пропускаются (как в JSON Schema)
template <typename Owner, size_t I, typename V>
[[nodiscard]] error_code check_value(const V& v) {
  if constexpr (std::is_same_v<V, std::string>) {
    return check_string<Owner, I>(v);
  } else if constexpr (is_number_value<V>) {
    return check_number<Owner, I>(to_bound(v));
  } else if constexpr (is_arrayof<V>) {
    return check_array<Owner, I>(v);
  } else if constexpr (is_optional_like<V>) {
    if (!v.has_value())
      return {};
    return check_value<Owner, I>(*v);
  } else if constexpr (requires { v.visit([](auto&&) {}); }) {  // oneof
    return v.visit([]<typename X>(const X& x) -> error_code {
      if constexpr (std::is_same_v<X, metel::nothing_t>)
        return {};
      else
        return check_value<Owner, I>(x);
    });
  } else if constexpr (an_object<V>) {
    return check_properties_count<Owner, I>(properties_count(v));
  } else if constexpr (requires { get_special_field(v); }) {
    // вложенный сгенерированный тип с одним значением
    return check_value<Owner, I>(get_special_field(v));
  } else {
    // bool, null, enum, anyof/allof: ограничения на них не применяются
    return {};
  }
}

}  // namespace noexport

// проверяет все ограничения типа `T` для разобранного значения `v`
template <has_constraints T>
[[nodiscard]] error_code check_constraints(const T& v) {
  constexpr size_t n = std::tuple_size_v<std::decay_t<decltype(T::constraints)>>;
  error_code ec;
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    auto one = [&]<size_t I>(std::index_sequence<I>) {
      if constexpr (an_object<T>)
        ec = noexport::check_properties_count<T, I>(noexport::properties_count(v));
      else
        ec = noexport::check_value<T, I>(get_special_field(v));
      return !ec.failed();
    };
    (one(std::index_sequence<Is>{}) && ...);
  }(std::make_index_sequence<n>{});
  return ec;
}

}  // namespace svarg::cxx_support
