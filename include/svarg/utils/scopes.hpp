#pragma once

#include <algorithm>
#include <string>
#include <cassert>
#include <vector>
#include <span>

namespace svarg {

// по сути json pointer с возможностью добавлять и убирать последний сегмент
// НО может быть невалидным json pointer, так как в add_scope можно подать и строки содержащие /
// в итоге получив нечто типа path//get/abc. Но такие сегменты тоже поддерживаются и корреткно удаляются
struct scopes_t {
 private:
  std::string scope;
  std::vector<size_t> prevszs;

 public:
  void add_scope(std::string_view str) {
    assert(!str.empty());
    prevszs.push_back(scope.size());
    scope.push_back('/');
    scope += str;
  }
  void remove_scope() {
    assert(!prevszs.empty());
    scope.resize(prevszs.back());
    prevszs.pop_back();
  }

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
      if (prevszs_.size() == 1) {
        return str.substr(prevszs_[0] + 1 /*skip `/` */);
      }
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
};

}  // namespace svarg
