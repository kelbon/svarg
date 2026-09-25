#pragma once

#include <bitset>
#include <string_view>
#include <span>

#include "svarg/errors.hpp"
#include "svarg/utils/safe_cast.hpp"

#include "svarg/yap/common.hpp"

#include <omo/token_feeder.hpp>

namespace yap {

template <typename T>
concept sax_events_consumer = requires(T& x, std::string_view str, int_t i, uint_t ui) {
  x.on_part(str);
  x.on_array_begin();
  x.on_array_end();
  x.on_object_begin();
  x.on_object_end();
  x.on_string(str);
  x.on_key(str);
  x.on_int64(i);
  x.on_uint64(ui);
  x.on_double(3.0);
  x.on_bool(true);
};

namespace noexport {

[[nodiscard]] inline std::string_view parse_str(In& it, In e) {
  assert(it != e);
  uint_t len = hpack::decode_integer(it, e, INT_WIDTH);
  if (len > std::distance(it, e))
    throw hpack::incomplete_data_error(len - std::distance(it, e));
  std::string_view ret((const char*)it, (const char*)it + len);
  it += len;
  return ret;
}

}  // namespace noexport

// отрезает от 'in' один `yap` объект и передаёт его в виде ивентов в `vtor`
template <sax_events_consumer V>
void parse_binary(In& it, In e, V& vtor) {
  if (it == e)
    throw hpack::incomplete_data_error(1);
  while (it != e) {
    switch (get_type(*it)) {
      case type_e::BOOL:
        vtor.on_bool(bool(*it & 1));
        ++it;
        return;
      case type_e::END_OBJECT:
        vtor.on_object_begin();
        vtor.on_object_end();
        ++it;
        return;
      case type_e::NONNEGATIVE_INT:
        vtor.on_uint64(hpack::decode_integer(it, e, INT_WIDTH));
        return;
      case type_e::NEGATIVE_INT:
        vtor.on_int64(-int_t(hpack::decode_integer(it, e, INT_WIDTH)));
        return;
      case type_e::FLOAT: {
        std::string_view repr = noexport::parse_str(it, e);
        double d = 0;
        auto [it, e] = std::from_chars(repr.data(), repr.data() + repr.size(), d);
        if (e != std::errc{} || it != repr.data() + repr.size())
          throw svarg::parse_error("cannot parse float, errc={}", std::to_underlying(e));
        vtor.on_double(d);
      }
        return;
      case type_e::STRING:
        vtor.on_string(noexport::parse_str(it, e));
        return;
      case type_e::ARRAY:
        vtor.on_array_begin();
        {
          uint_t sz = hpack::decode_integer(it, e, INT_WIDTH);
          for (uint_t i = 0; i < sz; ++i)
            parse_binary(it, e, vtor);
        }
        vtor.on_array_end();
        return;
      case type_e::KEY:
        vtor.on_object_begin();
        do {
          vtor.on_key(noexport::parse_str(it, e));
          parse_binary(it, e, vtor);
        } while (it != e && get_type(*it) == type_e::KEY);
        if (it == e)
          throw hpack::incomplete_data_error(1);  // END_OBJECT пропущен
        if (get_type(*it) != type_e::END_OBJECT)
          throw svarg::parse_error("object must end with END_OBJECT");
        ++it;  // END_OBJECT
        vtor.on_object_end();
        return;
    }
  }
}

template <typename T>
struct from_sax_tokens {
  // вызывается с уже готовым токеном, отдаёт управление не пропуская последний обработанный токен
  // static tokens_consumer operator()(T& v, sax_token& ctx, dd::with_stack_resource r);
};

template <typename T>
tokens_consumer make_sax_parser(T& out, sax_token& tok, dd::with_stack_resource r) {
  return from_sax_tokens<std::decay_t<T>>{}(out, tok, r);
}

// возвращает оставшуюся часть, парсит только первое value
template <typename T>
std::span<const byte_t> from_binary(std::span<const byte_t> binary, T& out, std::span<byte_t> buf = {}) {
  // TODO чек что на работе корректная версия dd::stack_resource (алигн)
  In it = binary.data();
  In e = binary.data() + binary.size();
  omo::token_feeder vtor(
      [&out](sax_token& tok, dd::with_stack_resource r) { return make_sax_parser(out, tok, r); }, buf);
  parse_binary(it, e, vtor);
  if (!vtor.is_done())
    throw hpack::incomplete_data_error(1);  // слишком мало данных
  return {it, e};
}

template <>
struct from_sax_tokens<bool> {
  static tokens_consumer operator()(bool& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::bool_);
    v = tok.get_bool();
    return {};
  }
};

template <>
struct from_sax_tokens<std::string> {
  static tokens_consumer operator()(std::string& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::string);
    v = tok.get_str();
    return {};
  }
};

template <typename T>
  requires(std::integral<T>)
struct from_sax_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    using enum sax_token::kind_e;
    switch (tok.kind()) {
      case int64:
        svarg::safe_write(tok.get_int64(), v);
        return {};
      case uint64:
        svarg::safe_write(tok.get_uint64(), v);
        return {};
      default:
        throw svarg::parse_error("expected int64/uint64");
    }
  }
};

template <typename T>
  requires(std::floating_point<T>)
struct from_sax_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::double_ | sax_token::int64 | sax_token::uint64);
    switch (tok.kind()) {
      case omo::sax_token::double_:
        v = T(tok.get_double());
        break;
      case omo::sax_token::int64:
        v = T(tok.get_int64());
        break;
      case omo::sax_token::uint64:
        v = T(tok.get_uint64());
        break;
      default:
        metel::unreachable();
    }
    return {};
  }
};

template <typename T>
struct from_sax_tokens<std::vector<T>> {
  static tokens_consumer operator()(std::vector<T>& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::array_begin);
    for (;;) {
      co_yield {};
      if (tok.kind() == sax_token::array_end)
        break;
      co_yield dd::elements_of(make_sax_parser(v.emplace_back(), tok, r));
    }
  }
};

template <typename T>
struct from_sax_tokens<std::optional<T>> {
  static tokens_consumer operator()(std::optional<T>& v, sax_token& tok, dd::with_stack_resource r) {
    if (tok.kind() == sax_token::null) [[unlikely]]
      return {};
    return make_sax_parser(v.emplace(), tok, r);
  }
};

template <typename T>
struct from_sax_tokens<metel::box<T>> {
  static tokens_consumer operator()(metel::box<T>& v, sax_token& tok, dd::with_stack_resource r) {
    if (tok.kind() == sax_token::null) [[unlikely]]
      return {};
    return make_sax_parser(v.emplace(), tok, r);
  }
};

template <svarg::aggregate T>
struct from_sax_tokens<T> {
 private:
  static constexpr size_t N = boost::pfr::tuple_size_v<T>;

  static consteval std::bitset<N> required_bits_set() {
    std::bitset<N> mask;

    APPLY_INDEXES(N) {
      (mask.set(Is, !is_optional_field_type<boost::pfr::tuple_element_t<Is, T>>), ...);
    };
    return mask;
  }

  static bool all_parsed(const std::bitset<N>& parsed_) {
    constexpr std::bitset mask = required_bits_set<T>();
    return (parsed_ & mask) == mask;
  }

  static tokens_consumer consumer_for_field(std::string_view key, T& v, sax_token& tok,
                                            std::bitset<N>& parsed, dd::with_stack_resource r) {
    tokens_consumer consumer;
    bool found = false;
    FOR_EACH_INDEX(boost::pfr::tuple_size_v<T>) {
      if (!found && key == boost::pfr::get_name<I, T>()) {
        consumer = make_sax_parser(boost::pfr::get<I>(v), tok, r);
        parsed.set(I);
        found = true;
      }
    };
    if (found)
      return consumer;
    return ignore_sax_value(tok, r);
  }

 public:
  // незнакомые поля игнорируются для обратной совместимости (TODO test)
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    std::bitset<N> parsed;
    constexpr std::bitset REQUIRED_MASK = required_bits_set();
    tok.expect(sax_token::object_begin);

    for (;;) {
      co_yield {};
      if (tok.kind() == sax_token::object_end)
        break;
      tok.expect(sax_token::key);
      std::string_view key = tok.get_key();
      co_yield {};
      co_yield dd::elements_of(consumer_for_field(key, v, tok, parsed, r));
    }
    if ((parsed & REQUIRED_MASK) != REQUIRED_MASK) [[unlikely]]
      throw svarg::parse_error("not all required fields are present");
  }
};

}  // namespace yap
