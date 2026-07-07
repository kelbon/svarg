#pragma once

#include <omo/stream_parser.hpp>
#include <metel/meta.hpp>

#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/concepts.hpp"
#include "svarg/backend/cxx/support/from_json_value.hpp"

namespace svarg::cxx_support {

using omo::sax_token;
using omo::sax_token_value;
using tokens_consumer = omo::tokens_consumer;

// читает схему из токенов
// НЕ делает валидацию. это происходит в логике уровнем выше
template <typename T>
struct from_tokens {
  // static tokens_consumer operator()(T& v, sax_token& ctx, dd::with_stack_resource r);
};

// заглушка, которая неэффективно, но делает "потоковый" парсинг собирая json value и переиспользуя
// from_json_value
template <typename T>
tokens_consumer from_tokens_fallback(T& v, sax_token& tok, dd::with_stack_resource r) {
  boost::json::value value;
  co_yield dd::elements_of(omo::json_value_collector(tok, value, r));
  error_code ec;
  from_json_value<T>::parse(v, value, ec);
  if (ec)
    ec.rethrow_if_failed();
}

// шорткат
// НЕ валидирует, так что его нужно использовать для верхнеуровневого парса,
// а для подобъектов всегда parse_and_validate
template <typename T>
tokens_consumer from_tokens_parser(T& v, sax_token& tok, dd::with_stack_resource r) {
  return from_tokens<std::remove_cvref_t<T>>{}(v, tok, r);
}

template <typename T>
omo::stream_parser json_stream_parser_for(T& out, std::span<unsigned char> buf = {}) {
  return omo::stream_parser(
      [&out](sax_token& tok, dd::with_stack_resource r) -> tokens_consumer {
        return from_tokens_parser(out, tok, r);
      },
      buf);
}

}  // namespace svarg::cxx_support
