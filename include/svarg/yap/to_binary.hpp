#pragma once

#include <set>
#include <unordered_set>
#include <charconv>

#include <boost/pfr.hpp>

#include "svarg/yap/common.hpp"

namespace yap {

namespace noexport {

[[nodiscard]] auto write_type(byte_t t, Out auto _out) {
  auto out = hpack::noexport::adapt_output_iterator(_out);
  *out = t;
  // без ++, чтобы записывать дальше в тот же байт
  return out;
}

// Не трогает первые `int_width` бит
template <Out It, int..., typename UINT>
[[nodiscard]] It write_uint(UINT u, int int_width, It out) {
  static_assert(std::is_same_v<UINT, uint_t>);
  return hpack::encode_integer<uint_t>(u, int_width, out);
}

template <int..., typename UINT>
[[nodiscard]] auto write_type_and_uint(byte_t t, UINT sz, int int_width, Out auto _out) {
  static_assert(std::is_same_v<UINT, uint_t>);
  auto out = hpack::noexport::adapt_output_iterator(_out);
  *out = t;
  return write_uint(sz, int_width, out);
}

template <Out It>
[[nodiscard]] It write_typed_string(yap01bits_e t, std::string_view str, It _out) {
  auto out = noexport::write_type_and_uint(byte_t(t), static_cast<uint_t>(str.size()), /*int_width=*/6, _out);
  out = hpack::noexport::copy_n_fast(str.data(), str.size(), out);
  return hpack::noexport::unadapt<It>(out);
}

}  // namespace noexport

template <typename T>
struct to_binary_repr {
  // template<Out It>
  // It operator()(const T&, It) const;
};

template <typename T, Out It>
[[nodiscard]] It to_binary(const T& x, It out) {
  return to_binary_repr<std::remove_cvref_t<T>>{}(x, out);
}

template <typename T>
void to_binary(const T& x, binary_t& out) {
  (void)to_binary(x, std::back_inserter(out));
}

template <typename T>
[[nodiscard]] binary_t to_binary(const T& x) {
  binary_t out;
  to_binary(x, out);
  return out;
}

// специализации

template <>
struct to_binary_repr<bool> {
  template <Out It>
  It operator()(bool v, It out) const {
    return to_binary(v ? uint_t(1) : uint_t(0), out);
  }
};

// без концептов и тд, только uint64 и int64,
// чтобы избежать ситуации "экономии одного байта", которая сломает обратную совместимость
template <>
struct to_binary_repr<uint_t> {
  template <Out It>
  It operator()(uint_t v, It _out) const {
    auto out = noexport::write_type_and_uint(
        byte_t(yap01bits_e::LOOK_NEXT2) | byte_t(yap23bits_e::NONNEGATIVE_INT), v, /*int_width=*/4, _out);
    return hpack::noexport::unadapt<It>(out);
  }
};

template <>
struct to_binary_repr<int_t> {
  template <Out It>
  It operator()(int_t v, It _out) const {
    if (v >= 0)
      return to_binary(uint_t(v), _out);
    auto out =
        noexport::write_type(byte_t(yap01bits_e::LOOK_NEXT2) | byte_t(yap23bits_e::NEGATIVE_INT), _out);
    assert(v != std::numeric_limits<int_t>::min());  // overflow
    out = noexport::write_uint(uint_t(-v), /*int_width=*/4, out);
    return hpack::noexport::unadapt<It>(out);
  }
};

template <>
struct to_binary_repr<std::string_view> {
  template <Out It>
  It operator()(std::string_view str, It out) const {
    return noexport::write_typed_string(yap01bits_e::STRING, str, out);
  }
};

template <>
struct to_binary_repr<std::string> {
  template <Out It>
  It operator()(const std::string& str, It out) const {
    return to_binary(std::string_view(str), out);
  }
};

template <>
struct to_binary_repr<huffman_encoded_string> {
  template <Out It>
  It operator()(const huffman_encoded_string& str, It out) const {
    std::string s;
    hpack::encode_string_huffman(str.str, std::back_inserter(s));
    return to_binary(std::string_view(s), out);
  }
};

template <>
struct to_binary_repr<double> {
  template <Out It>
  It operator()(double v, It out) const {
    char tmp[64];
    auto [ptr, ec] = std::to_chars(tmp, tmp + 64, v);
    if (ec != std::errc{}) [[unlikely]]
      throw svarg::serialize_error("something bad happen, errc={}", std::to_underlying(ec));
    return to_binary(std::string_view(+tmp, ptr), out);
  }
};

template <std::ranges::range R>
struct to_binary_repr<R> {
  template <Out It>
  It operator()(const R& arr, It _out) const {
    auto out = hpack::noexport::adapt_output_iterator(_out);
    *out = byte_t(yap23bits_e::ARRAY_BEGIN);
    ++out;
    for (const auto& elem : arr)
      out = to_binary(elem, out);
    *out = byte_t(yap23bits_e::ARRAY_END);
    ++out;
    return hpack::noexport::unadapt<It>(out);
  }
};

// box нужен для поддержки рекурсивных типов, там где optional сломает компиляцию
// а также для полей которые очень редко реально хранятся, но занимают много места
template <typename T>
struct to_binary_repr<metel::box<T>> {
  template <Out It>
  It operator()(const metel::box<T>& o, It out) const {
    if (!o)
      return out;
    return to_binary(*o, out);
  }
};

template <typename T>
struct to_binary_repr<std::optional<T>> {
  template <Out It>
  It operator()(const std::optional<T>& o, It out) const {
    if (!o)
      return out;
    return to_binary(*o, out);
  }
};

namespace noexport {

template <svarg::aggregate T, size_t I, Out It>
[[nodiscard]] It yap_serialize_field(const T& in, It out) {
  auto write_key = [](It out) {
    constexpr std::string_view name = boost::pfr::get_name<I, T>();
    if constexpr (is_indexed_keys<T>) {
      out = noexport::write_type(byte_t(yap01bits_e::KEY_INDEX), out);
      out = noexport::write_uint(uint_t(I), /*int_width=*/6, out);
    } else {
      out = noexport::write_typed_string(yap01bits_e::KEY, name, out);
    }
  };

  const auto& field = boost::pfr::get<I>(in);
  if constexpr (is_optional_field_type<boost::pfr::tuple_element_t<I, T>>) {
    if (!field.has_value())
      return out;  // empty optional feild
    write_key(out);
    return to_binary(*field, out);
  } else {
    write_key(out);
    return to_binary(field, out);
  }
}

}  // namespace noexport

template <yap_object T>
struct to_binary_repr<T> {
  static_assert(std::is_same_v<T, std::decay_t<T>>);

  template <Out It>
  It operator()(const T& in, It _out) const {
    auto out = hpack::noexport::adapt_output_iterator(_out);
    FOR_EACH_INDEX(boost::pfr::tuple_size_v<T>) {
      if constexpr (!is_skipped_field<boost::pfr::tuple_element_t<I, T>>::value)
        out = noexport::yap_serialize_field<T, I>(in, out);
    };
    *out = std::to_underlying(yap23bits_e::OBJECT_END);
    ++out;
    return hpack::noexport::unadapt<It>(out);
  }
};

}  // namespace yap
