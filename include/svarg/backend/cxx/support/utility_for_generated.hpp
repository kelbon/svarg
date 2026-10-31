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
  static_assert(fields_count<std::remove_const_t<T>>() == 1);
  // <special_field_name>
  return in._dATA;
}

// additional_properties

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

template <typename T>
consteval bool valid_generated_object() {
  static_assert(metel::is_decayed_v<T>);
  if (!an_object<T>)
    return false;

  if constexpr (has_additional_properties<T>) {
    // лежит последним полем и с правильным типом
    return boost::pfr::get_name<boost::pfr::tuple_size_v<T> - 1>() == additional_properties_field_name &&
           std::is_same_v<boost::pfr::tuple_element_t<boost::pfr::tuple_size_v<T> - 1, T>,
                          additional_properties_schema_t<T>>;
  }
  return true;
}

// возвращает битовую маску где единицы расставлены на места требуемых полей
template <cxx_support::an_object O>
consteval std::bitset<fields_count<O>()> get_required_bits_mask() {
  std::bitset<fields_count<O>()> mask;

  APPLY_INDEXES(fields_count<O>()) {
    (mask.set(Is, O::is_required(cxx_support::get_field_name<Is, O>())), ...);
  };
  return mask;
}

template <an_object A>
constexpr std::optional<size_t> get_field_index(std::string_view field_name) {
  return APPLY_INDEXES(fields_count<A>()) {
    using switch_t = ss::string_switch<size_t>;
    return (switch_t(field_name) | ... | switch_t::case_t(get_field_name<Is, A>(), Is)).or_nullopt();
  };
}

}  // namespace svarg::cxx_support
