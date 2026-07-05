#include "svarg/backend/cxx/support/concepts.hpp"

#include <metel/macro.hpp>

namespace svarg::cxx_support {

tokens_consumer sax_ignore_value(sax_token& tok) {
  switch (tok.got) {
    case sax_token::string:
    case sax_token::int64:
    case sax_token::uint64:
    case sax_token::double_:
    case sax_token::bool_:
    case sax_token::null:
      co_return;
    default:
      break;
  }
  size_t depth = 0;
  for (;;) {
    switch (tok.got) {
      case sax_token::array_begin:
      case sax_token::object_begin:
        depth++;
        co_yield {};
        break;
      case sax_token::part:
        metel::unreachable();
      case sax_token::array_end:
      case sax_token::object_end:
        depth--;
        if (depth == 0)
          co_return;
        [[fallthrough]];
      case sax_token::string:
      case sax_token::int64:
      case sax_token::uint64:
      case sax_token::double_:
      case sax_token::bool_:
      case sax_token::null:
      case sax_token::key:
        co_yield {};
    }
  }
}

}  // namespace svarg::cxx_support
