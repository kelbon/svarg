#pragma once

#include "svarg/frontend/parameter_location.hpp"
#include "svarg/ir/ir_object.hpp"

namespace svarg::ir {

// просто проходит по всему по дефолту, включая в свои перегрузки и перегрузки наследника
// Note: наследник должен не забыть сделать using ir_visitor_base::operator()
struct ir_visitor_base {
 protected:
  template <typename T>
  void operator()(this auto&& self, const std::optional<T>& x) {
    if (x)
      self(*x);
  }
  template <typename T>
  void operator()(this auto&& self, const optional<T>& x) {
    if (x)
      self(*x);
  }
  void operator()(this auto&& self, const aggregate auto& x) {
    boost::pfr::for_each_field(x, self);
  }
  template <typename... Types>
  void operator()(this auto&& self, const std::set<Types...>& x) {
    for (auto&& v : x)
      self(v);
  }
  template <typename... Types>
  void operator()(this auto&& self, const std::unordered_map<Types...>& x) {
    for (auto&& [_, v] : x)
      self(v);
  }
  template <typename... Types>
  void operator()(this auto&& self, const std::map<Types...>& x) {
    for (auto&& [_, v] : x)
      self(v);
  }
  template <typename T>
  void operator()(this auto&& self, const arrayof<T>& x) {
    for (auto&& v : x)
      self(v);
  }
  template <typename... Types>
  void operator()(this auto&& self, const std::variant<Types...>& x) {
    std::visit(self, x);
  }
  void operator()(this auto&& self, const string&) {
  }
  void operator()(this auto&& self, const content_type_t& c) {
    self(string(c.str()));
  }
  void operator()(this auto&& self, const bool&) {
  }
  void operator()(this auto&& self, const Any&) {
  }
  void operator()(this auto&& self, const ir::fundamental_e&) {
  }
  void operator()(this auto&& self, const json_pointer&) {
  }
  void operator()(this auto&& self, const parameter_location&) {
  }
  void operator()(this auto&& self, const status_range_e&) {
  }
  void operator()(this auto&& self, const int64_t&) {
  }
  void operator()(this auto&& self, const uint64_t&) {
  }
  void operator()(this auto&& self, const number&) {
  }

 public:
  void visit(this auto&& self, auto&& x) {
    self(std::as_const(x));
  }
};

template <typename Foo>
struct schema_ref_vtor : ir_visitor_base {
  Foo f;

  explicit schema_ref_vtor(Foo f) : f(std::move(f)) {
  }

  using ir_visitor_base::operator();

  void operator()(const schema_ref& s) {
    // схемы не влияют на кеш и сравнения в сетах, поэтому const cast здесь нормален
    f(const_cast<schema_ref&>(s));
  }
};

}  // namespace svarg::ir
