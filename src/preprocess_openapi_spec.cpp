
#include <boost/json/value.hpp>
#include "svarg/openapi.hpp"

namespace svarg {

// TODO план
// препроцессирование
// тест на парс спеки
// объект содержащий сразу все спеки нужные
// резолв ссылок на нём

// если в ref нет # (разделителя на путь до файла и json поинтер) это означает include json/yaml по URI
// делает блокирующий GET запрос по URL с/без SSL (url должно начинаться с http:// или https://)
// а затем парсит ответ как json
static boost::json::value request_refered_object(std::string_view url) {
#if 0
  // TODO кеш этих запросов url -> boost::json::value
  // TODO если там .yaml то надо парсить как yaml
  bool is_yaml = url.ends_with(".yaml") || url.ends_with(".yml");
  // TODO get request etc
  if (is_yaml) {
    std::string json = yaml2json(rsp.body);
    // TODO
  }
  // TODO смотреть внутрь объекта и резолвить дальше, если есть ссылки, при этом ref должны быть относительно
  // нового объекта
#endif
  return {};
}

boost::json::value preprocess_openapi_spec(boost::json::value v, const svarg_options& options) {
  // TODO: резолвить ссылки без #. В том числе видимо URL
  return v;
}

}  // namespace svarg
