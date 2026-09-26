#pragma once

#include <bitset>
#include <string_view>
#include <span>

#include "hpack/decoder.hpp"
#include "svarg/errors.hpp"
#include "svarg/utils/safe_cast.hpp"

#include "svarg/yap/common.hpp"

#include <omo/token_feeder.hpp>

namespace yap {

// бросание исключения в корутине на винде может привести к сегфолту. Эта функция прячет исключение на стек
// неинлайненной функции и таким образом обходит баг
[[gnu::noinline]] [[noreturn]] void throw_it(auto v) {
  throw std::move(v);
}

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
  uint_t len = hpack::decode_integer(it, e, /*int width=*/6);
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
  auto parse_yap_object = [&]<bool Indexed>(std::bool_constant<Indexed>) {
    // внутри одного объекта нельзя смешивать KEY/KEY_INDEX (так не закодирует никогда)
    constexpr yap01bits_e K = Indexed ? yap01bits_e::KEY_INDEX : yap01bits_e::KEY;
    vtor.on_object_begin();
    while (it != e && get01bits(*it) == K) {
      if constexpr (Indexed) {
        vtor.on_uint64(hpack::decode_integer(it, e, /*int width=*/6));
      } else {
        vtor.on_key(noexport::parse_str(it, e));
      }
      parse_binary(it, e, vtor);
    }
    if (it == e)
      throw hpack::incomplete_data_error(1);  // OBJECT_END пропущен
    if (*it != byte_t(yap23bits_e::OBJECT_END))
      throw svarg::parse_error("object must end with OBJECT_END");
    ++it;
    vtor.on_object_end();
  };

  if (it == e)
    throw hpack::incomplete_data_error(1);
  switch (get01bits(*it)) {
    case yap01bits_e::KEY:
      return parse_yap_object(std::false_type{});
    case yap01bits_e::KEY_INDEX:
      return parse_yap_object(std::true_type{});
    case yap::yap01bits_e::STRING:
      vtor.on_string(noexport::parse_str(it, e));
      return;
    default:
      switch (get23bits(*it)) {
        case yap23bits_e::OBJECT_END:
          vtor.on_object_begin();
          vtor.on_object_end();
          ++it;
          return;
        case yap23bits_e::NONNEGATIVE_INT:
          vtor.on_uint64(hpack::decode_integer(it, e, /*int width=*/4));
          return;
        case yap23bits_e::NEGATIVE_INT:
          vtor.on_int64(-int_t(hpack::decode_integer(it, e, /*int width=*/4)));
          return;
        case yap23bits_e::ARRAY_BEGIN_OR_END:
          if (*it == byte_t(yap23bits_e::ARRAY_BEGIN)) {
            ++it;
            vtor.on_array_begin();
            while (it != e && *it != byte_t(yap23bits_e::ARRAY_END))
              parse_binary(it, e, vtor);
            if (it == e || *it != byte_t(yap23bits_e::ARRAY_END))
              throw svarg::parse_error("array must end with ARRAY_END");
            ++it;
            vtor.on_array_end();
            return;
          } else if (*it == byte_t(yap23bits_e::ARRAY_END)) {
            // нельзя интерпретировать как ARRAY_BEGIN + ARRAY_END, так как во вложенных массивах появляется
            // неоднозначность
            ++it;
            throw hpack::protocol_error("ARRAY_END without ARRAY_BEGIN");
          }
          [[fallthrough]];
        default:
          throw hpack::protocol_error("invalid byte sequence, cannot be parsed as 'yap'");
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
    tok.expect(sax_token::bool_ | sax_token::int64 | sax_token::uint64);
    // bool кодируется как int в yap, к тому же ради обратной совместимости имеет смысл сделать так
    switch (tok.kind()) {
      case omo::sax_token::int64:
        v = bool(tok.get_int64());
        break;
      case omo::sax_token::uint64:
        v = bool(tok.get_uint64());
        break;
      case omo::sax_token::bool_:
        v = tok.get_bool();
        break;
      default:
        metel::unreachable();
    }
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

template <>
struct from_sax_tokens<huffman_encoded_string> {
  static tokens_consumer operator()(huffman_encoded_string& v, sax_token& tok, dd::with_stack_resource) {
    tok.expect(sax_token::string);
    std::string_view repr = tok.get_str();
    In in = (const byte_t*)repr.data();
    In e = in + repr.size();
    hpack::decoded_string destr;
    hpack::decode_string(in, e, destr);
    v.str = destr.str();
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
        throw_it(svarg::parse_error("expected int64/uint64"));
    }
  }
};

template <typename T>
  requires(std::floating_point<T>)
struct from_sax_tokens<T> {
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    tok.expect(sax_token::double_ | sax_token::int64 | sax_token::uint64 | sax_token::string);
    switch (tok.kind()) {
      case sax_token::double_:
        v = T(tok.get_double());
        break;
      case sax_token::int64:
        v = T(tok.get_int64());
        break;
      case sax_token::uint64:
        v = T(tok.get_uint64());
        break;
      case sax_token::string: {
        // в yap флоаты кодируются строками, к тому же ради обратной совместимости имеет смысл сделать так
        std::string_view repr = tok.get_str();
        auto [it, e] = std::from_chars(repr.data(), repr.data() + repr.size(), v);
        if (e != std::errc{} || it != repr.data() + repr.size())
          throw_it(svarg::parse_error("cannot parse float, errc={}", std::to_underlying(e)));
      } break;
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

template <yap_object T>
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

  static tokens_consumer consumer_for_field(std::string_view key, T& v, sax_token& tok,
                                            std::bitset<N>& parsed, dd::with_stack_resource r) {
    tokens_consumer consumer;
    bool found = false;
    FOR_EACH_INDEX(boost::pfr::tuple_size_v<T>) {
      if constexpr (!is_skipped_field<boost::pfr::tuple_element_t<I, T>>::value) {
        if (!found && key == boost::pfr::get_name<I, T>()) {
          if (parsed[I])
            throw_it(svarg::parse_error("field `{}` parsed twice", key));
          consumer = make_sax_parser(boost::pfr::get<I>(v), tok, r);
          parsed.set(I);
          found = true;
        }
      } else {
        parsed.set(I);
      }
    };
    if (found)
      return consumer;
    return ignore_sax_value(tok, r);
  }

  static tokens_consumer consumer_for_field(uint64_t index, T& v, sax_token& tok, std::bitset<N>& parsed,
                                            dd::with_stack_resource r) {
    tokens_consumer consumer;
    bool found = false;
    FOR_EACH_INDEX(boost::pfr::tuple_size_v<T>) {
      if constexpr (!is_skipped_field<boost::pfr::tuple_element_t<I, T>>::value) {
        if (!found && index == I) {
          if (parsed[I])
            throw_it(svarg::parse_error("field `{}` parsed twice", boost::pfr::get_name<I, T>()));
          consumer = make_sax_parser(boost::pfr::get<I>(v), tok, r);
          parsed.set(I);
          found = true;
        }
      } else {
        parsed.set(I);
      }
    };
    if (found)
      return consumer;
    return ignore_sax_value(tok, r);
  }

 public:
  // незнакомые поля игнорируются для обратной совместимости
  static tokens_consumer operator()(T& v, sax_token& tok, dd::with_stack_resource r) {
    std::bitset<N> parsed;
    constexpr std::bitset REQUIRED_MASK = required_bits_set();
    tok.expect(sax_token::object_begin);

    for (;;) {
      co_yield {};
      if (tok.kind() == sax_token::object_end)
        break;
      tok.expect(sax_token::key | sax_token::uint64);
      if (tok.kind() == sax_token::key) {
        std::string_view key = tok.get_key();
        co_yield {};
        co_yield dd::elements_of(consumer_for_field(key, v, tok, parsed, r));
      } else {
        auto index = tok.get_uint64();
        co_yield {};
        co_yield dd::elements_of(consumer_for_field(index, v, tok, parsed, r));
      }
    }
    if ((parsed & REQUIRED_MASK) != REQUIRED_MASK) [[unlikely]]
      throw_it(svarg::parse_error("not all required fields are present"));
  }
};

}  // namespace yap
