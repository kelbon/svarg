#pragma once

#include <http2/http_body.hpp>
#include <http2/http2_client.hpp>
#include <http2/http_base.hpp>

namespace svarg::cxx_support {

using http2::http2_client;
using http2::http_body;
using http2::http_body_bytes;
using http2::http_header_t;
using http2::http_headers_t;
using http2::http_method_e;
using http2::http_request;
using http2::http_response;
using http2::request_context;

// TODO добавить HTTP коды енам, его e2str/efromstr и в самом http2 вынести это в отдельный неймспейс

}  // namespace svarg::cxx_support
