#include "svarg/utils/parse_url.hpp"

namespace svarg {

static size_t scheme_length(std::string_view url) {
  if (url.empty() || !is_scheme_char(url[0], /*first=*/true))
    return 0;
  for (size_t i = 1; i < url.size(); ++i) {
    if (url[i] == ':')
      return i;
    if (!is_scheme_char(url[i], /*first=*/false))
      return 0;
  }
  return 0;
}

url_ref parse_absolute_url(std::string_view url) {
  url_ref result;

  size_t schemeLen = scheme_length(url);
  if (schemeLen == 0)
    throw std::invalid_argument("URI must have a scheme");
  if (url.size() < schemeLen + 3 || url.compare(schemeLen, 3, "://") != 0)
    throw std::invalid_argument("URI must have \"://\" after scheme");
  result.scheme = url.substr(0, schemeLen);
  url = url.substr(schemeLen + 3);

  // 2. Authority vs path/query/fragment
  auto authorityEnd = url.find_first_of("/?#");
  std::string_view authorityStr;
  if (authorityEnd == std::string_view::npos) {
    authorityStr = url;
    result.path = "/";
  } else {
    authorityStr = url.substr(0, authorityEnd);
    result.path = url.substr(authorityEnd);  // может начинаться с '?' или '#'
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
