#pragma once

#include "svarg/concepts.hpp"

#include <bitset>
#include <boost/pfr.hpp>

#include "svarg/backend/cxx/support/concepts.hpp"

namespace svarg::cxx_support {

// TODO добавить здесь про дискриминаторы и всякое нужное для render, связать через этот файл render.cpp с
// cxx_support

// metel::visit_index для любого числа полей (у metel не больше 128): вызывает f.template operator()<i>(),
// i < Count. В спецификациях встречаются объекты и query с сотнями параметров
template <size_t Count, size_t Offset = 0>
constexpr decltype(auto) visit_field_index(auto&& f, size_t i) {
  static_assert(Count > Offset);
  constexpr size_t chunk = metel::VISIT_INDEX_MAX + 1;
  auto shifted = [&]<size_t I>() -> decltype(auto) { return f.template operator()<Offset + I>(); };
  if constexpr (Count - Offset <= chunk) {
    return metel::visit_index<Count - Offset - 1>(shifted, i - Offset);
  } else {
    if (i - Offset < chunk)
      return metel::visit_index<chunk - 1>(shifted, i - Offset);
    return visit_field_index<Count, Offset + chunk>(f, i);
  }
}

// имя максимально кривое для наименьшей вероятности пересечения с другими именами
static constexpr std::string_view special_field_name = "_implfield";

template <typename T>
[[nodiscard]] constexpr auto& get_special_field(T& in) noexcept {
  static_assert(fields_count<std::remove_const_t<T>>() == 1);
  // <special_field_name>
  return in._implfield;
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
  if constexpr (T::is_required(get_field_name<I, T>())) {
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
    (mask.set(Is, O::is_required(cxx_support::get_field_name<Is, O>())), ...);
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
