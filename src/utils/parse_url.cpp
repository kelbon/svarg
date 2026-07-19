#include "svarg/utils/parse_url.hpp"

namespace svarg {

static std::size_t scheme_length(std::string_view uri) {
  if (uri.empty() || !is_scheme_char(uri[0], /*first=*/true))
    return 0;
  for (std::size_t i = 1; i < uri.size(); ++i) {
    if (uri[i] == ':')
      return i;
    if (!is_scheme_char(uri[i], /*first=*/false))
      return 0;
  }
  return 0;
}

url_ref parse_absolute_url(std::string_view uri) {
  url_ref result;

  size_t schemeLen = scheme_length(uri);
  if (schemeLen == 0)
    throw std::invalid_argument("URI must have a scheme");
  if (uri.size() < schemeLen + 3 || uri.compare(schemeLen, 3, "://") != 0)
    throw std::invalid_argument("URI must have \"://\" after scheme");
  result.scheme = uri.substr(0, schemeLen);
  uri = uri.substr(schemeLen + 3);

  // 2. Authority vs path/query/fragment
  auto authorityEnd = uri.find_first_of("/?#");
  std::string_view authorityStr;
  if (authorityEnd == std::string_view::npos) {
    authorityStr = uri;
    result.path = "/";
  } else {
    authorityStr = uri.substr(0, authorityEnd);
    result.path = uri.substr(authorityEnd);  // может начинаться с '?' или '#'
  }
  auto atPos = authorityStr.rfind('@');
  if (atPos != std::string_view::npos) {
    result.userinfo = authorityStr.substr(0, atPos);
    authorityStr = authorityStr.substr(atPos + 1);
  }

  // 4. IPv6
  if (!authorityStr.empty() && authorityStr.front() == '[') {
    auto closeBracket = authorityStr.find(']');
    if (closeBracket == std::string_view::npos)
      throw std::invalid_argument("Malformed IPv6 address: missing ']'");
    result.host = authorityStr.substr(1, closeBracket - 1);
    result.is_ipv6 = true;
    authorityStr = authorityStr.substr(closeBracket + 1);
    if (!authorityStr.empty()) {
      if (authorityStr.front() != ':')
        throw std::invalid_argument("Unexpected character after IPv6 address");
      result.port = authorityStr.substr(1);
    }
  } else {
    // 5. host[:port]
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
