#pragma once

#include <algorithm>
#include <string>
#include <cassert>
#include <vector>
#include <span>

#include <boost/json/value.hpp>

#include "svarg/utils/from_chars.hpp"
#include "svarg/utils/path_tools.hpp"

namespace svarg {

struct escaped_t {
  explicit escaped_t() = default;
};

// заставляет вставить сегменты как есть
constexpr inline escaped_t escaped = escaped_t{};
// TODO rename file / test

// TODO нужно хранить экранировано, т.к. могут совпасть "/" + "name" и add_key("/name")
// TODO escaped_json_pointer(str) - вьюшка

// по сути json pointer с возможностью добавлять и убирать последний сегмент
// НО может быть невалидным json pointer, так как в add_scope можно подать и строки содержащие /
// в итоге получив нечто типа path//get/abc. Но такие сегменты тоже поддерживаются и корреткно удаляются
struct json_pointer {
 private:
  // хранится экранировано, чтобы было невозможно пересечение путей
  // и можно было обходить через visit_path_segments
  std::string data;
  std::vector<size_t> prevszs;

 public:
  json_pointer() = default;
  // precondition: строка начинается с '/' или пуста (json pointer)
  explicit json_pointer(std::convertible_to<std::string_view> auto&& s) {
    std::string_view str(s);
    add_json_ptr(str);
  }

  // precondition: str распарсится в сегменты нормально
  explicit json_pointer(std::convertible_to<std::string_view> auto&& s, escaped_t) {
    std::string_view str(s);
    add_json_ptr(str, ::svarg::escaped);
  }

  // json key
  void add_key(std::string_view key) {
    // хранится без escape
    add_one_scope(key);
  }
  // array index
  void add_index(size_t i) {
    char stri[64];
    char* e = std::format_to(stri, "{}", i);
    add_one_scope(std::string_view(stri, e));
  }

  // str валидный json pointer
  void add_json_ptr(std::string_view str) {
    if (!str.contains('/'))
      return add_one_scope(str);
    visit_path_segments(str, [&](std::string_view seg) { add_one_scope(seg); });
  }
  void add_json_ptr(std::string_view str, escaped_t) {
    if (!str.contains('/'))
      return add_one_scope(str);
    visit_path_segments(str, [&](std::string_view seg) { add_one_scope(seg, ::svarg::escaped); });
  }

  void rm_last_segment() {
    assert(!prevszs.empty());
    data.resize(prevszs.back());
    prevszs.pop_back();
  }

  // возвращает экранированный сегмент
  std::string last_segment() const noexcept {
    if (empty())
      return {};
    return jp_unescape_segment(std::string_view(data).substr(prevszs.back() + 1 /*skip /*/));
  }

  // возвращает ESCAPED указатель
  [[nodiscard]] std::string_view view() const noexcept {
    return data;
  }

  [[nodiscard]] size_t segments_count() const noexcept {
    return prevszs.size();
  }

  [[nodiscard]] bool empty() const noexcept {
    return prevszs.empty();
  }

  struct iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = value_type;

   private:
    std::string_view str;
    std::span<const size_t> prevszs_;

   public:
    iterator() noexcept = default;

    explicit iterator(const json_pointer& s) noexcept : str(s.data), prevszs_(s.prevszs) {
    }

    reference operator*() const noexcept {
      assert(!prevszs_.empty() && "end iterator dereference");
      if (prevszs_.size() == 1)
        return str.substr(prevszs_[0] + 1 /*skip `/` */);
      assert(prevszs_[1] > prevszs_[0]);
      size_t seglen = prevszs_[1] - prevszs_[0];
      return str.substr(prevszs_[0] + 1 /*skip `/` */, seglen - 1);
    }

    iterator& operator++() noexcept {
      prevszs_ = prevszs_.subspan(1);
      return *this;
    }

    iterator operator++(int) noexcept {
      auto copy = *this;
      ++*this;
      return copy;
    }

    bool operator==(const iterator& r) const noexcept {
      return str == r.str && std::ranges::equal(prevszs_, r.prevszs_);
    }
    bool operator==(std::default_sentinel_t) const noexcept {
      return prevszs_.empty();
    }
  };

  // begin/end вынесено сюда, чтобы нельзя было ошибиться по типу std::format("{}", jsonpre (без .view()))
  struct segments_t {
    const json_pointer& jsonptr;

    iterator begin() const noexcept {
      return iterator(jsonptr);
    }
    static std::default_sentinel_t end() noexcept {
      return std::default_sentinel;
    }
  };
  // возвращает экранированные сегменты
  [[nodiscard]] segments_t segments() const noexcept {
    return segments_t(*this);
  }

  // NOTE: сюда не должны попадать два и более сегментов за раз
  // если `key` это строковое представление индекса, то тоже сработает
  json_pointer& operator/=(std::string_view key) {
    add_key(key);
    return *this;
  }
  json_pointer& operator/=(size_t num) {
    add_index(num);
    return *this;
  }
  json_pointer& operator/=(const json_pointer& other) {
    for (std::string_view seg : other.segments())
      add_one_scope(seg, ::svarg::escaped);
    return *this;
  }
  template <typename Me>
  json_pointer operator/(this Me&& self, std::string_view key) {
    json_pointer s = std::forward<Me>(self);
    s /= key;
    return s;
  }
  template <typename Me>
  json_pointer operator/(this Me&& self, size_t i) {
    json_pointer s = std::forward<Me>(self);
    s /= i;
    return s;
  }

  std::string_view escaped() const {
    return data;
  }
  // может вернуть невалидный json pointer, но красивый
  std::string unescaped() const {
    if (empty())
      return {};
    std::string result;
    visit_path_segments(escaped(), [&](std::string_view seg) {
      result.push_back('/');
      result += jp_unescape_segment(seg);
    });
    return result;
  }

  // TODO тесты из json pointer rfc
  // TODO взятие сегмента по индексу (и замена)
  // TODO понять не будет ли description/прочая хрень из ссылки ломать парсинг каких-то типов
  // желательно наверное игнорировать их при override

  // получает доступ без escape/unescape
  boost::json::value* access(boost::json::value& json) const {
    auto r = json.try_at_pointer(data);
    return r ? &*r : nullptr;
    /*
    boost::json::value* current = &json;

    for (std::string_view escaped_seg : segments()) {
      std::string seg = jp_unescape_segment(escaped_seg);
      if (current->is_array()) {
        size_t idx;
        if (!try_from_chars(seg, idx))
          return nullptr;
        auto& arr = current->as_array();
        if (idx >= arr.size())
          return nullptr;
        current = &arr[idx];
      } else if (current->is_object()) {
        auto& obj = current->as_object();
        auto it = obj.find(seg);
        if (it == obj.end())
          return nullptr;
        current = &it->value();
      } else {
        return nullptr;
      }
    }
    return current;
    */
  }

  const boost::json::value* access(const boost::json::value& json) const {
    return access(const_cast<boost::json::value&>(json));
  }

  bool operator==(const json_pointer& other) const noexcept {
    return view() == other.view();
  }

 private:
  // precondition: str это только один сегмент (например "id", а не "id/name")
  void add_one_scope(std::string_view str) {
    assert(!str.empty());
    prevszs.push_back(data.size());
    data.push_back('/');
    // TODO лучше с экранированным ренжом, а не созданием строки
    if (!jp_escape_required(str))
      data += str;
    else
      data += jp_escape_segment(str);
  }
  void add_one_scope(std::string_view str, escaped_t) {
    assert(!str.empty());
    prevszs.push_back(data.size());
    data.push_back('/');
    data += str;
  }
};

}  // namespace svarg

inline svarg::json_pointer operator""_jp(const char* str, size_t sz) {
  return svarg::json_pointer(std::string_view(str, sz));
}
