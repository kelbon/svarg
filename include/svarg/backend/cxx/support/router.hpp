#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "svarg/backend/cxx/support/http_base.hpp"
#include "svarg/backend/cxx/support/route_trie.hpp"

// МАРШРУТИЗАЦИЯ: метод и путь -> id маршрута. Что за id и какой обработчик ему соответствует, решает владелец
// роутера (сгенерированный сервер - индекс операции, пользователь - свой).
//
// Для каждого метода по порядку:
//   1. пути без шаблонов ("/health", "/v1/items") - хеш-таблица, выигрывают у всех
//   2. список "ДО" - шаблоны проверяются линейно (match_path_pattern) в порядке добавления, выигрывает первый
//      подошедший. Для смешанных сегментов ("/files/{name}.{ext}", "/v{version}"), которых нет в trie.
//      Значение любого параметра здесь может содержать '/' ("/files/{name}.{ext}" подходит к
//      "/files/dir/a.txt"), так что шаблон "ДО" перехватывает и пути с большим числом сегментов
//   3. шаблоны с параметрами на весь сегмент - route_trie. Последний параметр забирает остаток пути вместе с
//      '/' ("/repos/{owner}/{repo}/contents/{path}" и path = src/main.cpp), если ничего точнее не подошло
//
// Значения параметров роутер не возвращает: владелец знает шаблон маршрута по id и разбирает путь по нему
// (сгенерированный сервер - в from_request). Поэтому find ничего не хранит и не выделяет память (кроме списка
// "ДО", там match_path_pattern тоже без аллокаций).
//
// Роутер не хранит обработчиков, поэтому маршруты можно добавлять и удалять когда угодно, в том числе пока
// обрабатывается запрос, найденный по удаляемому маршруту.
//
// TODO для сгенерированного сервера литеральные пути можно проверять string switch, без хеш-таблицы

namespace svarg::cxx_support {

struct router {
  using id_t = route_trie::id_t;

  // путь без шаблонов или с параметрами на весь сегмент ("/items/{id}/data"). Тот же метод и та же форма
  // пути уже есть или смешанный сегмент (такие - через add_before) - исключение
  void add_route(hidi::http_method_e, std::string_view pattern, id_t);
  // ничего не делает, если такого маршрута нет
  void remove_route(hidi::http_method_e, std::string_view pattern);

  // в конец списка "ДО" метода. Любой шаблон, в том числе со смешанными сегментами. Тот же шаблон уже есть
  // или он невалиден - исключение
  void add_before(hidi::http_method_e, std::string_view pattern, id_t);
  // ничего не делает, если такого шаблона нет
  void remove_before(hidi::http_method_e, std::string_view pattern);

  // `path` - путь запроса без query, не декодированный
  [[nodiscard]] std::optional<id_t> find(hidi::http_method_e, std::string_view path) const;

 private:
  static constexpr size_t methods_count = size_t(hidi::http_method_e::UNKNOWN) + 1;

  struct before_entry {
    std::string pattern;
    id_t id;
  };

  std::array<std::unordered_map<std::string, id_t, str_hash, str_equal_to>, methods_count> literal_paths;
  std::array<std::vector<before_entry>, methods_count> before;
  std::array<route_trie, methods_count> templates;
};

}  // namespace svarg::cxx_support
