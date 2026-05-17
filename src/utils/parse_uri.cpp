#include "svarg/utils/parse_uri.hpp"

namespace svarg {

// написано клаудом
parsed_uri parse_uri(std::string_view uri) {
  parsed_uri result;

  // 1. Scheme
  auto schemeEnd = uri.find("://");
  if (schemeEnd == std::string_view::npos) {
    throw std::invalid_argument("URI must have a scheme");
  }
  result.scheme = uri.substr(0, schemeEnd);
  uri = uri.substr(schemeEnd + 3);

  // 2. Authority / path
  auto pathStart = uri.find('/');
  std::string_view authorityStr;
  if (pathStart == std::string_view::npos) {
    authorityStr = uri;
    result.path = "/";
  } else {
    authorityStr = uri.substr(0, pathStart);
    result.path = uri.substr(pathStart);
  }

  // 3. IPv6
  if (!authorityStr.empty() && authorityStr.front() == '[') {
    auto closeBracket = authorityStr.find(']');
    if (closeBracket == std::string_view::npos) {
      throw std::invalid_argument("Malformed IPv6 address: missing ']'");
    }
    result.host = authorityStr.substr(0, closeBracket + 1);
    authorityStr = authorityStr.substr(closeBracket + 1);
    if (!authorityStr.empty()) {
      if (authorityStr.front() != ':') {
        throw std::invalid_argument("Unexpected character after IPv6 address");
      }
      result.port = authorityStr.substr(1);
    }
  } else {
    // 4. host[:port]
    auto colonPos = authorityStr.find(':');
    if (colonPos != std::string_view::npos) {
      result.host = authorityStr.substr(0, colonPos);
      result.port = authorityStr.substr(colonPos + 1);
    } else {
      result.host = authorityStr;
    }
  }

  return result;
}

}  // namespace svarg
