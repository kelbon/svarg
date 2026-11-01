#include "svarg/backend/cxx/support/router.hpp"

#include <algorithm>
#include <vector>

#include "svarg/errors.hpp"
#include "svarg/utils/path_tools.hpp"

namespace svarg::cxx_support {

// ROUTE_TRIE

namespace {

// "/a/{id}/b" -> {"a", "{id}", "b"}, "/" -> {""}
std::vector<std::string_view> split_path_segments(std::string_view path) {
  std::vector<std::string_view> segments;
  if (path.starts_with('/'))
    path.remove_prefix(1);
  for (;;) {
    size_t slash = path.find('/');
    segments.push_back(path.substr(0, slash));
    if (slash == std::string_view::npos)
      return segments;
    path.remove_prefix(slash + 1);
  }
}

}  // namespace

void route_trie::add(std::string_view pattern, id_t id) {
  if (!pattern.starts_with('/'))
    throw validate_error("route pattern `{}` must start with '/'", pattern);
  std::vector<std::string_view> segments = split_path_segments(pattern);
  // проверка до вставки, чтобы ошибка не оставила пустых узлов
  for (std::string_view seg : segments)
    if (!is_param_segment(seg) && (seg.contains('{') || seg.contains('}')))
      throw validate_error("route pattern `{}`: segment `{}` mixes literal and parameter", pattern, seg);
  node* n = &root;
  for (std::string_view seg : segments) {
    if (is_param_segment(seg)) {
      if (!n->param)
        n->param = std::make_unique<node>();
      n = n->param.get();
    } else {
      auto& child = n->literals[std::string(seg)];
      if (!child)
        child = std::make_unique<node>();
      n = child.get();
    }
  }
  // все узлы уже были (иначе n->id пуст), поэтому ничего не создано зря
  if (n->id)
    throw validate_error("route pattern `{}` conflicts with an existing route of the same shape", pattern);
  n->id = id;
}

void route_trie::remove(std::string_view pattern) {
  // путь от корня, чтобы удалить опустевшие узлы снизу вверх
  std::vector<std::pair<node*, std::string_view>> path;
  node* n = &root;
  for (std::string_view seg : split_path_segments(pattern)) {
    path.emplace_back(n, seg);
    if (is_param_segment(seg)) {
      n = n->param.get();
    } else {
      auto it = n->literals.find(seg);
      n = it == n->literals.end() ? nullptr : it->second.get();
    }
    if (!n)
      return;
  }
  n->id.reset();
  while (!path.empty() && n->empty()) {
    auto [parent, seg] = path.back();
    path.pop_back();
    if (is_param_segment(seg))
      parent->param.reset();
    else
      parent->literals.erase(parent->literals.find(seg));
    n = parent;
  }
}

std::optional<route_trie::id_t> route_trie::match(const node& n, std::string_view rest, bool has_more) {
  if (!has_more)
    return n.id;
  size_t slash = rest.find('/');
  std::string_view seg = rest.substr(0, slash);
  std::string_view next = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
  bool next_has_more = slash != std::string_view::npos;
  if (auto it = n.literals.find(seg); it != n.literals.end())
    if (std::optional id = match(*it->second, next, next_has_more))
      return id;
  // литеральная ветка не дошла до конца - параметр. Пустой сегмент тоже значение: пустая строка или пустой
  // массив (/cells/x/), его проверит разбор параметра
  if (n.param)
    return match(*n.param, next, next_has_more);
  return std::nullopt;
}

std::optional<route_trie::id_t> route_trie::find(std::string_view path) const {
  if (root.empty())
    return std::nullopt;
  if (path.starts_with('/'))
    path.remove_prefix(1);
  return match(root, path, /*has_more=*/true);
}

// ROUTER

void router::add_route(hidi::http_method_e method, std::string_view pattern, id_t id) {
  size_t m = size_t(method);
  if (pattern.contains('{') || pattern.contains('}')) {
    templates[m].add(pattern, id);
    return;
  }
  if (!pattern.starts_with('/'))
    throw validate_error("route pattern `{}` must start with '/'", pattern);
  if (!literal_paths[m].try_emplace(std::string(pattern), id).second)
    throw validate_error("route {} `{}` already exists", e2str(method), pattern);
}

void router::remove_route(hidi::http_method_e method, std::string_view pattern) {
  size_t m = size_t(method);
  if (pattern.contains('{') || pattern.contains('}')) {
    templates[m].remove(pattern);
    return;
  }
  if (auto it = literal_paths[m].find(pattern); it != literal_paths[m].end())
    literal_paths[m].erase(it);
}

void router::add_before(hidi::http_method_e method, std::string_view pattern, id_t id) {
  if (!pattern.starts_with('/'))
    throw validate_error("route pattern `{}` must start with '/'", pattern);
  if (!validate_maybe_templated_path_parameters(pattern))
    throw validate_error("route pattern `{}`: invalid parameters", pattern);
  auto& list = before[size_t(method)];
  if (std::ranges::contains(list, pattern, &before_entry::pattern))
    throw validate_error("route {} `{}` already exists", e2str(method), pattern);
  list.push_back(before_entry{std::string(pattern), id});
}

void router::remove_before(hidi::http_method_e method, std::string_view pattern) {
  auto& list = before[size_t(method)];
  std::erase_if(list, [&](const before_entry& e) { return e.pattern == pattern; });
}

std::optional<router::id_t> router::find(hidi::http_method_e method, std::string_view path) const {
  size_t m = size_t(method);
  if (m >= methods_count)
    return std::nullopt;
  if (auto it = literal_paths[m].find(path); it != literal_paths[m].end())
    return it->second;
  for (const before_entry& e : before[m])
    if (match_path_pattern(e.pattern, path))
      return e.id;
  return templates[m].find(path);
}

}  // namespace svarg::cxx_support
