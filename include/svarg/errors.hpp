#pragma once

#include <format>

#include <metel/meta.hpp>
#include "metel/box_union.hpp"

#include "utils/type_name.hpp"

namespace svarg {

struct svarg_exception : std::exception {
  std::string message;

  template <typename... Args>
  explicit svarg_exception(std::format_string<Args...> fmt, Args&&... args)
      : message(std::format(fmt, std::forward<Args>(args)...)) {
  }
  // pre: p != nullptr
  explicit svarg_exception(const char* p) : message(p) {
  }

  const char* what() const noexcept override {
    return message.c_str();
  }
};

struct parse_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct validate_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct semantic_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct render_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct serialize_error : svarg_exception {
  using svarg_exception::svarg_exception;
  using svarg_exception::operator=;
};

struct old_message_t {
  explicit old_message_t() = default;
};
// тег для error_code::reformat обозначающий какой аргумент
// форматной строки подменить на старое сообщение еррор кода
constexpr inline old_message_t old_message = old_message_t{};

namespace noexport {

template <typename T>
using strview_if_old_msg =
    std::conditional_t<std::is_same_v<std::decay_t<T>, old_message_t>, std::string_view, T>;

}

#define SVARG_ALL_EXCEPTIONS parse_error, validate_error, semantic_error, render_error, serialize_error

struct METEL_TRIVIAL_ABI error_code {
 private:
  // аллокация в случае ошибки, это снижает плату в случае если ошибки нет
  // всего 8 байт и тривиальное аби для всего еррор кода
  metel::box_union<SVARG_ALL_EXCEPTIONS> err;

 public:
  error_code() = default;

  template <metel::oneof_types<SVARG_ALL_EXCEPTIONS> Err>
  error_code(Err e) : err(std::move(e)) {
  }

  bool failed() const noexcept {
    return !!err;
  }
  explicit operator bool() const noexcept {
    return failed();
  }

  std::string_view message() const noexcept {
    metel::matcher m{
        [](const metel::nothing_t&) { return std::string_view(""); },
        [](const auto& x) { return std::string_view(x.message); },
    };
    return err.visit(m);
  }

  template <typename Me>
  void rethrow_if_failed(this Me&& self) {
    metel::matcher m{
        [](metel::nothing_t) {},
        [](auto&& x) { throw std::forward_like<Me>(x); },
    };
    self.err.visit(m);
  }

  // переформатирует старое сообщение, см. `old_message_t`
  // pre: failed() == true
  template <typename... Args>
  void reformat(std::format_string<noexport::strview_if_old_msg<Args>...> fmt, Args&&... args) {
    assert(failed());
    metel::matcher m{
        [](metel::nothing_t) {},
        [&](auto& x) { x.message = std::format(fmt, insert_old_message(std::forward<Args>(args))...); }};
    err.visit(m);
  }

  template <metel::oneof_types<SVARG_ALL_EXCEPTIONS> Err>
  void set(Err e) {
    err = std::move(e);
  }

 private:
  template <typename T>
  T&& insert_old_message(T&& t) noexcept {
    static_assert(!std::is_same_v<old_message_t, std::decay_t<T>>);
    return std::forward<T>(t);
  }
  std::string_view insert_old_message(const old_message_t&) {
    return message();
  }
};

template <typename E>
[[nodiscard]] parse_error bad_enum_value(std::string_view value) {
  return parse_error("value `{}` cannot be represented by enum `{}`", value, type_name<E>());
}

}  // namespace svarg
