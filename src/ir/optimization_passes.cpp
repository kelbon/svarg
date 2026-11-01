#include "svarg/ir/optimization_passes.hpp"
#include "svarg/ir/ir_visitor.hpp"
#include "svarg/utils/algo.hpp"

#include <ranges>
#include <print>

namespace svarg::ir {

// ограничиваем количество раз на всякий случай
#define MAYBE_EVER \
  int i = 0;       \
  i < 20;          \
  ++i

ir_object optimization_pipeline(ir_object ir, const ir_options& options) {
  // применяется даже без оптимизаций вообще
  ir = pass_rm_trivial_schemas(std::move(ir), options).ir;
  // меняет представление для пользователя, а не только упрощает
  ir = pass_open_enums(std::move(ir), options).ir;
  if (!options.enable_optimizations)
    return ir;
  size_t count = 0;
  auto do_pass = [&](auto* pass) {
    pass_result r = pass(std::move(ir), options);
    ir = std::move(r.ir);
    count += r.count;
  };
  for (MAYBE_EVER) {
    count = 0;

    do_pass(pass_combinators_flatten);
    do_pass(pass_transform_allofs);
    //  после объединения oneOf некоторые схемы могут оказаться неиспользованными
    //  а после преобразования anyOf<X> -> X может даже получиться тривиальная схема, которую надо заменить
    do_pass(pass_rm_trivial_schemas);
    do_pass(pass_rm_unused_schemas);

    // убирание бесполезных требований может помочь другим оптимизациям
    do_pass(pass_rm_useless_requirements);
    do_pass(pass_rm_trivial_schemas);
    do_pass(pass_rm_unused_schemas);

    if (!count)
      break;
  }
  assert(validate_ir_object(ir));
  return ir;
}

pass_result pass_rm_trivial_schemas(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return {std::move(ir), 0};
  ir.validate();
  auto it = ir.schemas->begin();
  auto e = ir.schemas->end();
  size_t count = 0;
  for (; it != e;) {
    auto& s = *it;
    if (s.is_trivial() && (!s.name || options.allow_rm_unused == remove_category_e::any)) {
      schema_ref replace = make_fundamental_ref(std::get<fundamental_e>(s.type.data));
      auto foo = [&](schema_ref& r) {
        if (r.refers_to(s)) {
          r = replace;
          ++count;
        }
      };
      schema_ref_vtor replacer(foo);
      replacer.visit(ir);
      it = ir.schemas->erase(it);
    } else {
      ++it;
    }
  }
  if (ir.schemas->empty())
    ir.schemas.reset();
  assert(validate_ir_object(ir));
  return {std::move(ir), count};
}

size_t do_pass_rm_unused_schemas(ir_object& ir, const ir_options& options) {
  if (options.allow_rm_unused == remove_category_e::noone || !ir.schemas)
    return 0;
  std::unordered_set<schema_ref> used = options.marked_used;

  for (const ir::schema& s : *ir.schemas) {
    auto foo = [&](const schema_ref& r) {
      // не складываем ссылки написанные в самом типе, чтобы саморекурсивные типы удалялись корректно
      if (!r.refers_to(s))
        used.insert(r);
    };
    schema_ref_vtor collector(foo);
    collector.visit(s);
  }
  auto foo = [&](const schema_ref& r) { used.insert(r); };
  schema_ref_vtor collector(foo);
  collector.visit(ir.operations);
  collector.visit(ir.webhooks);

  auto it = ir.schemas->begin();
  auto e = ir.schemas->end();
  size_t count = 0;
  for (; it != e;) {
    bool allowed_to_remove = [&] {
      switch (options.allow_rm_unused) {
        case remove_category_e::noone:
          return false;
        case remove_category_e::only_inline_schemas:
          return it->name == std::nullopt;
        case remove_category_e::any:
          return true;
      }
      metel::unreachable();
    }();

    if (!used.contains(it->ref()) && allowed_to_remove) {
      it = ir.schemas->erase(it);
      ++count;
    } else
      ++it;
  }
  if (ir.schemas->empty())
    ir.schemas.reset();
  assert(validate_ir_object(ir));
  return count;
}

pass_result pass_rm_unused_schemas(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return {std::move(ir), 0};
  ir.validate();
  size_t count = 0;
  for (MAYBE_EVER) {
    size_t c = do_pass_rm_unused_schemas(ir, options);
    if (c == 0)
      break;
    count += c;
  }
  return {std::move(ir), count};
}

struct access_schema {
  const schemas_t& schemas;

  const schema& operator()(const schema_ref& r) {
    assert(!r.is_fundamental());
    return *schemas.find(r);
  }
};

// на что заменить this_schema
using maybe_replace = std::optional<schema_ref>;

static maybe_replace flatten_not(schema& this_schema, not_schema& n, access_schema get, size_t& changes) {
  auto handle_not_fundamental = [&](fundamental_e f) -> maybe_replace {
    if (f == fundamental_e::never)
      return make_fundamental_ref(fundamental_e::any);  // not<@never@> -> @any@
    else if (f == fundamental_e::any)
      return make_fundamental_ref(fundamental_e::never);  // not<@any@> -> @never@
    return std::nullopt;
  };

  if (this_schema.has_requirements())
    return std::nullopt;
  if (std::optional f = n.type.if_fundamental())
    return handle_not_fundamental(*f);
  auto& ot = get(n.type);
  if (ot.has_requirements())
    return std::nullopt;
  if (auto* f = ot.type.if_not()) {
    return f->type;  // not<not<T>> -> T
  } else if (ot.type.is_fundamental()) {
    // пока не превращённый в тривиальную схему фундаментал
    return handle_not_fundamental(*ot.type.if_fundamental());
  }
  return std::nullopt;
}

// anyof/allof/oneof
// возвращает true если что-то изменилось
template <typename T>
static bool inline_into(T& to, access_schema get, schema* add_requirements) {
  auto condition = [&](const schema_ref& s) {
    if (s.is_fundamental())
      return false;
    const schema& t = get(s);
    if (!add_requirements && t.has_requirements())
      return false;
    return std::holds_alternative<T>(t.type.data);
  };

  // достаём все T из to.schemas

  arrayof<schema_ref> inlined;
  for (const schema_ref& r : to.schemas)
    if (condition(r))
      inlined.push_back(r);

  // все схемы всех T вставляем в to.schemas и удаляем заинлайненные схемы из to.schemas

  arrayof<schema_ref> inlined_schemas;
  for (schema_ref& ref : inlined) {
    auto& t = get(ref);
    if (add_requirements) {
      if (t.required)
        add_requirements->add_requirements(*t.required);
    } else
      assert(!t.has_requirements());
    append_range(inlined_schemas, std::get<T>(t.type.data).schemas);
  }
  std::erase_if(to.schemas, condition);
  append_range(to.schemas, std::move(inlined_schemas));

  remove_duplicates(to.schemas);
  return !inlined.empty();
}

// `this_schema` - схема в которой лежит оптимизируемый allOf
static maybe_replace flatten_allof(schema& this_schema, allof_schema& allof, access_schema get,
                                   size_t& changes, const ir_options& options) {
  // для allof требования складываются
  if (inline_into(allof, get, &this_schema))
    ++changes;
  if (allof.schemas.empty()) {
    // allof<> -> any (также как (A && ...) == true если A пусто)
    this_schema.type.data = fundamental_e::any;
    ++changes;
  } else if (allof.schemas.size() == 1 && !this_schema.has_requirements()) {
    if (!this_schema.name || options.allow_rm_unused == remove_category_e::any)
      return allof.schemas.front();  // allof<X> -> X
  }
  return std::nullopt;
}

static maybe_replace flatten_anyof(schema& this_schema, anyof_schema& anyof, access_schema get,
                                   size_t& changes, const ir_options& options) {
  if (inline_into(anyof, get, /*add_requirements=*/nullptr))
    ++changes;
  if (anyof.schemas.empty()) {
    // anyof<> -> never (также как (A || ...) == false если A пусто)
    this_schema.type.data = fundamental_e::never;
    ++changes;
  } else if (anyof.schemas.size() == 1 && !this_schema.has_requirements()) {
    if (!this_schema.name || options.allow_rm_unused == remove_category_e::any)
      return anyof.schemas.front();  // anyof<X> -> X
  }
  return std::nullopt;
}

static maybe_replace flatten_oneof(schema& this_schema, oneof_schema& oneof, access_schema get,
                                   size_t& changes, const ir_options& options) {
  if (inline_into(oneof, get, /*add_requirements=*/nullptr))
    ++changes;
  if (oneof.schemas.empty()) {
    // oneof<> -> never (также как (A || ...) == false если A пусто)
    this_schema.type.data = fundamental_e::never;
    ++changes;
  } else if (oneof.schemas.size() == 1 && !this_schema.has_requirements()) {
    if (!this_schema.name || options.allow_rm_unused == remove_category_e::any)
      return oneof.schemas.front();  // oneof<X> -> X
  }
  return std::nullopt;
}

static maybe_replace flatten_combinator(const schema& s, const schemas_t& all_schemas, size_t& changes,
                                        const ir_options& options) {
  auto& this_schema = const_cast<schema&>(s);
  access_schema accessor(all_schemas);
  matcher vtor{
      [&](not_schema& n) -> maybe_replace { return flatten_not(this_schema, n, accessor, changes); },
      [](array_schema& arr) -> maybe_replace { return std::nullopt; },
      [](object_schema&) -> maybe_replace { return std::nullopt; },
      [](enum_schema&) -> maybe_replace { return std::nullopt; },
      [](fundamental_e&) -> maybe_replace { return std::nullopt; },
      [](constraints_schema&) -> maybe_replace { return std::nullopt; },
      [&](allof_schema& af) -> maybe_replace {
        return flatten_allof(this_schema, af, accessor, changes, options);
      },
      [&](anyof_schema& anf) -> maybe_replace {
        return flatten_anyof(this_schema, anf, accessor, changes, options);
      },
      [&](oneof_schema& oof) -> maybe_replace {
        return flatten_oneof(this_schema, oof, accessor, changes, options);
      },
  };
  return std::visit(vtor, this_schema.type.data);
}

pass_result pass_combinators_flatten(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return {std::move(ir), 0};
  ir.validate();
  size_t count = 0;
  for (MAYBE_EVER) {
    size_t changes = 0;
    auto it = ir.schemas->begin();
    auto e = ir.schemas->end();
    while (it != e) {
      std::optional replace = flatten_combinator(*it, *ir.schemas, changes, options);
      if (replace) {
        ++changes;
        assert(!replace->refers_to(*it));
        auto vtor = [&](schema_ref& r) {
          if (r.refers_to(*it))
            r = *replace;
        };
        schema_ref_vtor replacer(vtor);
        replacer.visit(ir);
        it = ir.schemas->erase(it);
      } else {
        ++it;
      }
    }
    if (!changes)
      break;
  }
  if (ir.schemas->empty())
    ir.schemas.reset();
  assert(validate_ir_object(ir));
  return {std::move(ir), count};
}

pass_result pass_open_enums(ir_object ir, const ir_options&) {
  if (!ir.schemas)
    return {std::move(ir), 0};
  access_schema get(*ir.schemas);
  auto is_string = [](const schema_ref& r) { return r.is_fundamental(fundamental_e::string); };
  // перечисление строк без требований: требования сузили бы "любую строку"
  auto string_enum = [&](const schema_ref& r) -> const enum_schema* {
    if (r.is_fundamental())
      return nullptr;
    const schema& s = get(r);
    const enum_schema* e = s.type.if_enum();
    if (!e || !is_string(e->type) || (e->open && *e->open) || s.has_requirements())
      return nullptr;
    return e;
  };
  size_t count = 0;
  for (const schema& cs : *ir.schemas) {
    const anyof_schema* any = cs.type.if_anyof();
    if (!any || any->discriminator || any->schemas.size() != 2)
      continue;
    const schema_ref& a = any->schemas[0];
    const schema_ref& b = any->schemas[1];
    const enum_schema* e = is_string(a) ? string_enum(b) : is_string(b) ? string_enum(a) : nullptr;
    if (!e)
      continue;
    enum_schema open = *e;
    open.open = true;
    // ключ множества (spec, jsonptr) не меняется
    const_cast<schema&>(cs).type.data = std::move(open);
    ++count;
  }
  return {std::move(ir), count};
}

// a subset b ?
static bool is_subset(const arrayof<schema_ref>& a, const arrayof<schema_ref>& b) {
  return std::ranges::all_of(a, [&](const schema_ref& need) { return std::ranges::contains(b, need); });
}

static std::optional<size_t> find_weak(const arrayof<schema_ref>& reqs, access_schema get) {
  // never покрывает всех,
  // allof<A, B> покрывает все allof<B, C, D> подмножества
  // anyof с меньшим числом аргументов перекрывает другой anyof
  // all проигрывает всем и вылетает

  // TODO? здесь потенциально очень много случаев типа oneof + anyof, allof + oneof и так далее
  // но их обработка не стоит того

  auto is_never = [](const schema_ref& r) {
    auto f = r.if_fundamental();
    return f && *f == fundamental_e::never;
  };
  auto never_it = std::ranges::find_if(reqs, is_never);
  if (never_it != reqs.end()) {
    auto it = std::ranges::find_if_not(reqs, is_never);
    if (it != reqs.end())
      return std::distance(reqs.begin(), it);
    else
      return std::nullopt;
  }

  for (size_t i = 0; i < reqs.size(); ++i) {
    const schema_ref& ri = reqs[i];

    if (auto f = ri.if_fundamental()) {
      if (*f == fundamental_e::any)
        return i;
      continue;
    }
    const schema& si = get(ri);

    if (const allof_schema* ai = si.type.if_allof()) {
      for (size_t j = 0; j < reqs.size(); ++j) {
        if (j == i || reqs[j].is_fundamental())
          continue;
        const schema& sj = get(reqs[j]);
        const allof_schema* aj = sj.type.if_allof();
        if (!aj)
          continue;
        // i = allOf<A, B, C>, j = allOf<A, B> => allOf<A, B, C>
        if (is_subset(aj->schemas, ai->schemas))
          return j;
      }
    }
    if (const anyof_schema* ai = si.type.if_anyof()) {
      for (size_t j = 0; j < reqs.size(); ++j) {
        if (j == i || reqs[j].is_fundamental())
          continue;
        const schema& sj = get(reqs[j]);
        const anyof_schema* aj = sj.type.if_anyof();
        if (!aj)
          continue;
        // i = anyOf<A, B>, j = anyOf<A, B, C> => anyof<A, B>
        if (is_subset(ai->schemas, aj->schemas))
          return j;
      }
    }
    if (const oneof_schema* ai = si.type.if_oneof()) {
      for (size_t j = 0; j < reqs.size(); ++j) {
        if (j == i || reqs[j].is_fundamental())
          continue;
        const schema& sj = get(reqs[j]);
        const oneof_schema* aj = sj.type.if_oneof();
        if (!aj)
          continue;
        // i = oneOf<A, B>, j = oneOf<A, B, C> => oneof<A, B>
        if (is_subset(ai->schemas, aj->schemas))
          return j;
      }
    }
  }

  return std::nullopt;
}

pass_result pass_rm_useless_requirements(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return {std::move(ir), 0};
  ir.validate();
  access_schema get(*ir.schemas);
  size_t count = 0;
  for (const schema& _s : *ir.schemas) {
    // требования не влияют на кеширование
    schema& s = const_cast<schema&>(_s);

    if (s.required) {
      // требование типа `any` (например allOf: [{pattern: a}, {pattern: b}] рядом с type: string) пропускает
      // любое значение, поэтому эквивалентно только своим требованиям. Они переносятся во владельца, и
      // значение не нужно разбирать повторно
      schema_refs_t flattened;
      for (size_t i = 0; i < s.required->size();) {
        const schema_ref r = (*s.required)[i];
        const fundamental_e* f = r.is_fundamental() ? nullptr : get(r).type.if_fundamental();
        if (!f || *f != fundamental_e::any || r.refers_to(s)) {
          ++i;
          continue;
        }
        s.required->erase(s.required->begin() + i);
        flattened.insert(r);
        ++count;
        if (const schema& inner = get(r); inner.has_requirements()) {
          for (const schema_ref& x : *inner.required)
            if (!flattened.contains(x) && !std::ranges::contains(*s.required, x))
              s.required->push_back(x);
        }
      }
      while (std::optional i = find_weak(*s.required, get)) {
        s.required->erase(s.required->begin() + *i);
        ++count;
      }
      if (s.required->empty())
        s.required.reset();
    }
  }
  assert(validate_ir_object(ir));
  return {std::move(ir), count};
}

template <typename... Args>
static void emit_warn(std::format_string<Args...> fmt, Args&&... args) {
  std::println(fmt, std::forward<Args>(args)...);
}

[[nodiscard]] pass_result pass_transform_allofs(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return {std::move(ir), 0};
  ir.validate();
  size_t count = 0;

  access_schema get(*ir.schemas);
  auto is_fundamental_schema = [&](const schema_ref& r) {
    return r.is_fundamental() || get(r).type.is_fundamental();
  };
  auto get_fundamental = [&](const schema_ref& r) -> fundamental_e {
    assert(is_fundamental_schema(r));
    if (r.is_fundamental())
      return *r.if_fundamental();
    return *get(r).type.if_fundamental();
  };
  auto is_a_fundamental = [=](fundamental_e e) {
    return [=](const schema_ref& r) { return get_fundamental(r) == e; };
  };
  auto is_object_schema = [&](const schema_ref& r) {
    return !r.is_fundamental() && get(r).type.is_object();  //
  };
  auto get_object = [&](const schema_ref& r) -> const object_schema& {
    assert(is_object_schema(r));
    return *get(r).type.if_object();
  };
  auto replace_with_never = [&](schema& s, std::string_view err) {
    s.type.data = fundamental_e::never;
    ++count;
    emit_warn("warning: allOf {}#{} {}, replaced with `schema: false`", s.spec, s.jsonptr.escaped(), err);
  };

  for (const schema& _s : *ir.schemas) {
    // spec/jsonptr не меняются
    schema& s = const_cast<schema&>(_s);
    allof_schema* allof = s.type.if_allof();
    if (!allof)
      continue;
    if (std::ranges::any_of(allof->schemas, is_fundamental_schema) &&
        std::ranges::any_of(allof->schemas, is_object_schema)) {
      replace_with_never(s, "contains both objects and fundamental types");
      continue;
    }
    if (std::ranges::all_of(allof->schemas, is_fundamental_schema)) {
      // если в allOf есть never - он невалидный
      if (std::ranges::any_of(allof->schemas, is_a_fundamental(fundamental_e::never))) {
        replace_with_never(s, "contains never");
        continue;
      }
      // allOf из фундаментальных типов сводится к одному фундаментальному типу, а требования схем (например
      // ограничения minLength/pattern) складываются, иначе при удалении схемы из allOf они бы потерялись
      arrayof<schema_ref> requirements;
      std::vector<fundamental_e> types;
      for (const schema_ref& r : allof->schemas) {
        if (!r.is_fundamental() && get(r).has_requirements())
          append_range(requirements, *get(r).required);
        types.push_back(get_fundamental(r));
      }
      // any не влияет на представление в allof
      std::erase(types, fundamental_e::any);
      // если что-то integer оно всегда number, но если что-то number оно не всегда integer
      // т.е. allof<integer, number> всегда сводится к integer
      if (std::ranges::contains(types, fundamental_e::integer))
        std::erase(types, fundamental_e::number);
      remove_duplicates(types);
      // здесь могли остаться только bool/null/string/number/integer, при этом number+integer
      // одновременно быть не могут любая комбинация конфликтует, например allOf<bool, null> - эквивалентно
      // never
      if (types.size() > 1) {
        replace_with_never(s, "has conflicting types");
        continue;
      }
      s.type.data = types.empty() ? fundamental_e::any : types.front();
      if (!requirements.empty()) {
        s.add_requirements(requirements);
        remove_duplicates(*s.required);
      }
      ++count;
    } else if (std::ranges::all_of(allof->schemas, is_object_schema)) {
      object_schema obj;
      for (auto& var : allof->schemas) {
        obj.combine(get_object(var));
        // требования складываются
        if (get(var).has_requirements())
          s.add_requirements(*get(var).required);
      }
      ++count;
      // заменяем allof на один объект
      s.type.data = std::move(obj);
    }
  }
  assert(validate_ir_object(ir));
  return {std::move(ir), count};
}

}  // namespace svarg::ir
