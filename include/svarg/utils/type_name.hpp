#pragma once
#include <string_view>

#include <anyany/noexport/type_descriptor_details.hpp>

namespace svarg {

namespace noexport {

template <typename T>
consteval std::string_view type_name_in_signature() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
  return __FUNCSIG__;
#else
  return __PRETTY_FUNCTION__;
#endif
}

inline constexpr std::string_view type_name_probe = type_name_in_signature<double>();
inline constexpr size_t type_name_prefix = type_name_probe.find("double");
inline constexpr size_t type_name_suffix =
    type_name_probe.size() - type_name_prefix - std::string_view("double").size();

}  // namespace noexport

// не идеально, но для дебага пойдёт
template <typename T>
consteval std::string_view type_name() noexcept {
  std::string_view sig = noexport::type_name_in_signature<T>();
  sig.remove_prefix(noexport::type_name_prefix);
  sig.remove_suffix(noexport::type_name_suffix);
  return sig;
}

}  // namespace svarg
