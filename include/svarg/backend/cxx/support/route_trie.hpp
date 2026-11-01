#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "svarg/utils/strcmp.hpp"

// trie шаблонов пути: "/items/{id}/data" -> id маршрута
//
// Сегмент шаблона (между '/') - литерал или параметр целиком ("{id}"). Смешанные сегменты ("{name}.{ext}")
// не поддерживаются, параметр - ровно один сегмент без '/'. Имена параметров не хранятся: /a/{x} и /a/{y} -
// одна и та же форма.
//
// При нескольких подходящих шаблонах выигрывает тот, у кого литерал раньше: /a/b/{y} у /a/{x}/b. Поиск -
// рекурсия по уровням, на каждом не больше двух вариантов: литерал, затем параметр, если литеральная ветка
// не дошла до конца (/items/special/info и /items/{id}/details: запрос /items/special/details находит второй)

namespace svarg::cxx_support {

struct route_trie {
  using id_t = size_t;

  // `pattern` начинается с '/'. Та же форма уже есть или смешанный сегмент - исключение
  void add(std::string_view pattern, id_t id);
  // ничего не делает, если такой формы нет
  void remove(std::string_view pattern);

  // `path` - путь запроса без query. Значения параметров не возвращаются: владелец маршрута знает свой шаблон
  // и разбирает их сам (сгенерированный сервер - в from_request). Память не выделяется
  [[nodiscard]] std::optional<id_t> find(std::string_view path) const;

  [[nodiscard]] bool empty() const noexcept {
    return root.empty();
  }

 private:
  struct node {
    std::unordered_map<std::string, std::unique_ptr<node>, str_hash, str_equal_to> literals;
    std::unique_ptr<node> param;
    std::optional<id_t> id;

    bool empty() const noexcept {
      return literals.empty() && !param && !id;
    }
  };
  node root;

  // `rest` - путь после очередного '/', `has_more` - остались ли сегменты ("/a/" -> "a", затем "")
  static std::optional<id_t> match(const node&, std::string_view rest, bool has_more);
};

// сегмент - параметр целиком: "{id}"
[[nodiscard]] constexpr bool is_param_segment(std::string_view seg) noexcept {
  return seg.size() > 2 && seg.front() == '{' && seg.back() == '}' &&
         seg.find_first_of("{}", 1) == seg.size() - 1;
}

}  // namespace svarg::cxx_support
