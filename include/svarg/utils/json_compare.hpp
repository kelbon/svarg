#include <boost/json/value.hpp>

namespace svarg {

// так себе сравнение, но организующее хотя бы какой-то порядок, чтобы можно было сортировать
std::strong_ordering json_compare(const boost::json::value& a, const boost::json::value& b);

}  // namespace svarg
