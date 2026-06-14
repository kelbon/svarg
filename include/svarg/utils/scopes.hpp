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

struct unescaped_t {
  explicit unescaped_t() = default;
};

// заставляет вставить сегменты как есть
constexpr inline unescaped_t unescaped = unescaped_t{};

// по сути json pointer с возможностью добавлять и убирать последний сегмент
// НО может быть невалидным json pointer, так как в add_scope можно подать и строки содержащие /
// в итоге получив нечто типа path//get/abc. Но такие сегменты тоже поддерживаются и корреткно удаляются
// TODO rename в реально json_pointer
struct scopes_t {
 private:
  std::string scope;
  std::vector<size_t> prevszs;

 public:
  scopes_t() = default;
  // precondition: строка начинается с '/' или пуста (json pointer)
  explicit scopes_t(std::convertible_to<std::string_view> auto&& s) {
    std::string_view str(s);
    add_scopes(str);
  }

  // precondition: str распарсится в сегменты нормально
  explicit scopes_t(std::convertible_to<std::string_view> auto&& s, unescaped_t) {
    std::string_view str(s);
    add_scopes(str, unescaped);
  }

  // TODO отрефакторить лишние методы, переименовать и тд

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
  void add_scopes(std::string_view str) {
    if (!str.contains('/'))
      return add_one_scope(str);
    visit_path_segments(str, [&](std::string_view seg) { add_one_scope(seg); });
  }
  void add_scopes(std::string_view str, unescaped_t) {
    if (!str.contains('/'))
      return add_one_scope(str);
    visit_path_segments(str, [&](std::string_view seg) { add_one_scope(seg, unescaped); });
  }

  void remove_scope() {
    assert(!prevszs.empty());
    scope.resize(prevszs.back());
    prevszs.pop_back();
  }

  // возвращает UNESCAPED указатель (возможно там сегменты не так поделены и тд)
  [[nodiscard]] std::string_view view() const noexcept {
    return scope;
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

    explicit iterator(const scopes_t& s) noexcept : str(s.scope), prevszs_(s.prevszs) {
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

  // begin/end вынесено сюда, чтобы нельзя было ошибиться по типу std::format("{}", scopes (без .view()))
  struct segments_t {
    const scopes_t& scopes;

    iterator begin() const noexcept {
      return iterator(scopes);
    }
    static std::default_sentinel_t end() noexcept {
      return std::default_sentinel;
    }
  };

  [[nodiscard]] segments_t segments() const noexcept {
    return segments_t(*this);
  }

  // NOTE: сюда не должны попадать два и более сегментов за раз
  // если `key` это строковое представление индекса, то тоже сработает
  scopes_t& operator/=(std::string_view key) {
    add_key(key);
    return *this;
  }
  scopes_t& operator/=(size_t num) {
    add_index(num);
    return *this;
  }
  scopes_t& operator/=(const scopes_t& other) {
    for (std::string_view seg : other.segments())
      add_one_scope(seg, unescaped);
    return *this;
  }
  template <typename Me>
  scopes_t operator/(this Me&& self, std::string_view key) {
    scopes_t s = std::forward<Me>(self);
    s /= key;
    return s;
  }
  template <typename Me>
  scopes_t operator/(this Me&& self, size_t i) {
    scopes_t s = std::forward<Me>(self);
    s /= i;
    return s;
  }

  std::string escaped() const {
    std::string result;
    if (scope.empty())
      return result;
    visit_path_segments(scope, [&](std::string_view seg) {
      if (jp_escape_required(seg))
        result += jp_escape_segment(seg);
    });
    return result;
  }

  // TODO тесты из json pointer rfc

  // получает доступ без escape/unescape
  boost::json::value* access(boost::json::value& json) const {
    boost::json::value* current = &json;

    for (std::string_view seg : segments()) {
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
  }

  const boost::json::value* access(const boost::json::value& json) const {
    return access(const_cast<boost::json::value&>(json));
  }

 private:
  // precondition: str это только один сегмент (например "id", а не "id/name")
  void add_one_scope(std::string_view str) {
    assert(!str.empty());
    prevszs.push_back(scope.size());
    scope.push_back('/');
    if (!jp_unescape_required(str))
      scope += str;
    else
      scope += jp_unescape_segment(str);
  }
  void add_one_scope(std::string_view str, unescaped_t) {
    assert(!str.empty());
    prevszs.push_back(scope.size());
    scope.push_back('/');
    scope += str;
  }
};

}  // namespace svarg

inline svarg::scopes_t operator""_jp(const char* str, size_t sz) {
  return svarg::scopes_t(std::string_view(str, sz));
}
