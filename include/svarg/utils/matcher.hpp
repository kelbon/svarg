#pragma once

#include <type_traits>
#include <variant>

namespace svarg {

template <typename... Foos>
struct matcher : Foos... {
  using Foos::operator()...;
};

template <typename... Foos>
matcher(Foos...) -> matcher<Foos...>;

// same as 'matcher', but casts every returned value to type Ret,
// so you dont need duplicate return type on every function
template <typename Ret, typename... Foos>
constexpr auto matcher_r(Foos&&... foos) noexcept(
    (... && std::is_nothrow_constructible_v<std::remove_cvref_t<Foos>, Foos&&>)) {
  return [m = matcher{std::forward<Foos>(foos)...}](auto&&... args) mutable -> Ret {
    // - uses RVO, no move
    // - do not uses std::invoke for better compilation speed/errors
    // and no one literaly needed to pass member pointers here
    return static_cast<Ret>(m(std::forward<decltype(args)>(args)...));
  };
}

template <typename Variant, typename... Foos>
decltype(auto) visit_one(Variant&& v, Foos&&... foos) {
  return std::visit(matcher(std::forward<Foos>(foos)...), std::forward<Variant>(v));
}

}  // namespace svarg
