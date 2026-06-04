#pragma once

#include <unordered_map>

namespace svarg {

// делает a.merge(b) НО если ключ есть и в А и в Б, то в результате будет ключ из Б
// при этом это эффективнее чем for (...) { insert_or_assign } и не инвалидирует ссылки на элементы
template <typename... Args>
[[nodiscard]] std::unordered_map<Args...> merge_with_override(std::unordered_map<Args...> a,
                                                              std::unordered_map<Args...> b) {
  // удаляем все возможно дублирующиеся ключи из Б в А
  for (auto& [k, _] : b)
    a.erase(k);
  a.merge(std::move(b));
  return a;
}

}  // namespace svarg
