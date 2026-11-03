#pragma once

#include "svarg/concepts.hpp"

#include <bitset>
#include <boost/pfr.hpp>

#include "svarg/backend/cxx/support/concepts.hpp"

namespace svarg::cxx_support {

template <size_t Count, typename F>
constexpr decltype(auto) visit_field_index(F&& f, size_t i) {
  if constexpr (Count > 0)
    // инстанцируется до Max включительно
    return metel::visit_index<Count - 1>(std::forward<F>(f), i);
  else
    return;
}


template <typename T>
[[nodiscard]] constexpr auto& get_special_field(T& in) noexcept {
  static_assert(fields_count<std::remove_const_t<T>>() == 1);
  // <special_field_name>
  return in.svarg_implfield;
}

// additional_properties

template <typename T>
using additional_properties_schema_t = typename std::decay_t<T>::svarg_additional_properties_type;


template <typename T>
constexpr auto& get_additional_properties(T& x) noexcept {
  static_assert(std::is_same_v<decltype(x.svarg_additional_properties),
                               additional_properties_t<additional_properties_schema_t<T>>>);
  return x.svarg_additional_properties;
}

// object

template <typename T>
consteval bool valid_generated_object() {
  static_assert(metel::is_decayed_v<T>);
  if (!an_object<T>)
    return false;

  if constexpr (has_additional_properties<T> && has_generated_fields<T>) {
    // поля доступны через svarg_field, положение additional properties не важно
    return std::is_same_v<decltype(T::svarg_additional_properties),
                          additional_properties_t<additional_properties_schema_t<T>>>;
  } else if constexpr (has_additional_properties<T>) {
    // для pfr лежит последним полем и с правильным типом
    constexpr size_t last = boost::pfr::tuple_size_v<T> - 1;
    return boost::pfr::get_name<last, T>() == additional_properties_field_name &&
           std::is_same_v<boost::pfr::tuple_element_t<last, T>,
                          additional_properties_t<additional_properties_schema_t<T>>>;
  }
  return true;
}

// необязательное поле - optional/box или oneof (пустое состояние), поле со значением по умолчанию
// хранится как есть и присутствует всегда
template <size_t I, typename T>
[[nodiscard]] constexpr bool is_field_present(const T& t) noexcept {
  if constexpr (get_field_info<I, T>().required) {
    return true;
  } else {
    const auto& f = get_field<I>(t);
    using F = std::remove_cvref_t<decltype(f)>;
    if constexpr (requires { f.has_value(); })
      return f.has_value();
    else if constexpr (requires { f.is_null(); })
      return !f.is_null();
    else if constexpr (a_oneof<F>)
      return !get_special_field(f).is_null();
    else
      return true;
  }
}

// возвращает битовую маску где единицы расставлены на места требуемых полей
template <cxx_support::has_named_fields O>
consteval std::bitset<fields_count<O>()> get_required_bits_mask() {
  std::bitset<fields_count<O>()> mask;

  APPLY_INDEXES(fields_count<O>()) {
    (mask.set(Is, cxx_support::get_field_info<Is, O>().required), ...);
  };
  return mask;
}

template <has_named_fields A>
constexpr std::optional<size_t> get_field_index(std::string_view field_name) {
  return APPLY_INDEXES(fields_count<A>()) {
    using switch_t = ss::string_switch<size_t>;
    return (switch_t(field_name) | ... | switch_t::case_t(get_field_name<Is, A>(), Is)).or_nullopt();
  };
}

}  // namespace svarg::cxx_support
