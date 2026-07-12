#pragma once

#include <boost/pfr.hpp>

// код немного продублирован чтобы меньше было в дебагере стек фреймов

namespace svarg {

// тип в котором поле лежит и номер
template <typename T, size_t I>
struct field_location {
  static constexpr std::string_view name() {
    return boost::pfr::get_name<I, T>();
  }
  using field_type = boost::pfr::tuple_element_t<I, T>;
};

// версия из boost pfr с кривыми перегрузками и в дебаге 10 слоёв
template <typename T, typename Fn, size_t... Is>
void for_each_field_location_impl(T&& v, Fn&& fn, std::index_sequence<Is...>) {
  (std::forward<Fn>(fn)(field_location<std::remove_cvref_t<T>, Is>{},
                        boost::pfr::get<Is>(std::forward<T>(v))),
   ...);
}

template <typename T, typename Fn, size_t... Is>
void for_each_field_name_impl(T&& v, Fn&& fn, std::index_sequence<Is...>) {
  (std::forward<Fn>(fn)(boost::pfr::get_name<Is, std::remove_cvref_t<T>>(),
                        boost::pfr::get<Is>(std::forward<T>(v))),
   ...);
}

// для каждого поля вызывает fn(имя, поле)
template <typename T, typename Fn>
void for_each_field_with_name(T&& v, Fn&& fn) {
  return for_each_field_name_impl(
      std::forward<T>(v), std::forward<Fn>(fn),
      std::make_index_sequence<boost::pfr::tuple_size_v<std::remove_reference_t<T>>>{});
}

}  // namespace svarg
