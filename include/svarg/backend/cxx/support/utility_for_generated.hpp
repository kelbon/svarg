#pragma once

#include "svarg/concepts.hpp"

#include <bitset>
#include <boost/pfr.hpp>

#include "svarg/backend/cxx/support/concepts.hpp"

namespace svarg::cxx_support {

// TODO добавить здесь про дискриминаторы и всякое нужное для render, связать через этот файл render.cpp с
// cxx_support

// имя максимально кривое для наименьшей вероятности пересечения с другими именами
static constexpr std::string_view special_field_name = "_dATA";

template <typename T>
[[nodiscard]] constexpr auto& get_special_field(T& in) noexcept {
  static_assert(boost::pfr::tuple_size_v<T> == 1);
  // <special_field_name>
  return in._dATA;
}

// additional_properties

template <typename T>
constexpr bool has_additional_properties = [] {
  if constexpr (requires { typename T::additional_properties_type; })
    return !std::is_void_v<typename T::additional_properties_type>;
  else
    return false;
}();

template <typename T>
using additional_properties_schema_t = typename std::decay_t<T>::additional_properties_type;

constexpr inline std::string_view additional_properties_schema_alias_name = "additional_properties_type";
constexpr inline std::string_view additional_properties_field_name = "_additional_properties";

template <typename T>
constexpr auto& get_additional_properties(T& x) noexcept {
  static_assert(std::is_same_v<decltype(x._additional_properties),
                               additional_properties_t<additional_properties_schema_t<T>>>);
  return x._additional_properties;
}

// object

template <aggregate A>
constexpr size_t object_fields_count() {
  // дополнительные лежат всегда последним полем и их нужно здесь игнорировать
  constexpr bool ignore_last_field = cxx_support::an_object<A> && cxx_support::has_additional_properties<A>();
  return boost::pfr::tuple_size_v<A> - size_t(ignore_last_field);
}

template <typename T>
consteval bool valid_generated_object() {
  static_assert(metel::is_decayed_v<T>);
  if (!an_object<T>)
    return false;

  if constexpr (has_additional_properties<T>) {
    // лежит последним полем и с правильным типом
    return boost::pfr::get_name<boost::pfr::tuple_size_v<T> - 1>() ==
               additional_properties_schema_alias_name &&
           std::is_same_v<boost::pfr::tuple_element_t<boost::pfr::tuple_size_v<T> - 1, T>,
                          additional_properties_schema_t<T>>;
  }
  return true;
}

// возвращает битовую маску где единицы расставлены на места требуемых полей
template <cxx_support::an_object O>
consteval std::bitset<object_fields_count<O>()> get_required_bits_mask() {
  std::bitset<object_fields_count<O>()> mask;

  auto helper = [&]<size_t... Is>(std::index_sequence<Is...>) {
    (mask.set(Is, O::is_required(boost::pfr::get_name<Is, O>())), ...);
  };
  helper(std::make_index_sequence<object_fields_count<O>()>{});
  return mask;
}

template <aggregate A>
constexpr std::optional<size_t> get_field_index(std::string_view field_name) {
  auto helper = [&]<size_t... Is>(std::index_sequence<Is...>) {
    using switch_t = ss::string_switch<size_t>;
    return (switch_t(field_name) | ... | switch_t::case_t(boost::pfr::get_name<Is, A>(), Is)).or_nullopt();
  };
  return helper(std::make_index_sequence<object_fields_count<A>()>{});
}

}  // namespace svarg::cxx_support
