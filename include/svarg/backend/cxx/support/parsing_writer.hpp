#pragma once

#include <omo/token_feeder.hpp>

#include "svarg/errors.hpp"
#include "svarg/backend/cxx/support/from_tokens.hpp"
#include "svarg/backend/cxx/support/to_tokens.hpp"

namespace svarg::cxx_support {

// принимает токены и снова собирает из них то же самое значение, одновременно валидируя
template <typename T>
struct parsing_writer : omo::token_feeder {
  explicit parsing_writer(T& out)
      : omo::token_feeder(
            [&out](sax_token& tok, dd::with_stack_resource r) -> tokens_consumer {
              return from_tokens_subobject(out, tok, r);
            },
            {}) {
  }

  // после последнего события
  void finish() const {
    if (!is_done())
      throw parse_error("value ended, but its parser did not");
  }
};

}  // namespace svarg::cxx_support
