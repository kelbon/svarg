#include "svarg/ir/optimization_passes.hpp"
#include "svarg/ir/ir_visitor.hpp"
#include "svarg/utils/algo.hpp"

#include <ranges>

namespace svarg::ir {

ir_object optimization_pipeline(ir_object ir, const ir_options& options) {
  // применяется даже без оптимизаций вообще
  ir = pass_rm_trivial_schemas(std::move(ir));
  if (!options.enable_optimizations)
    return ir;
  ir = pass_rm_unused_schemas(std::move(ir), options);
  ir = pass_combinators_flatten(std::move(ir), options);
  // после объединения oneOf некоторые схемы могут оказаться неиспользованными
  // а после преобразования anyOf<X> -> X может даже получиться тривиальная схема, которую надо заменить
  ir = pass_rm_unused_schemas(std::move(ir), options);
  ir = pass_rm_trivial_schemas(std::move(ir));
  return ir;
}

ir_object pass_rm_trivial_schemas(ir_object obj) {
  if (!obj.schemas)
    return obj;
  auto it = obj.schemas->begin();
  auto e = obj.schemas->end();
  for (; it != e;) {
    auto& s = *it;
    if (s.is_trivial()) {
      schema_ref replace = make_fundamental_ref(std::get<fundamental_e>(s.type.data));
      auto foo = [&](schema_ref& r) {
        if (r.refers_to(s))
          r = replace;
      };
      schema_ref_vtor replacer(foo);
      replacer.visit(obj);
      it = obj.schemas->erase(it);
    } else {
      ++it;
    }
  }
  return obj;
}

ir_object pass_rm_unused_schemas(ir_object ir, const ir_options& options) {
  if (!ir.schemas || !options.allow_rm_unused)
    return ir;
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
  for (; it != e;) {
    if (!used.contains(it->ref()))
      it = ir.schemas->erase(it);
    else
      ++it;
  }
  return ir;
}

struct access_schema {
  const schemas_t& schemas;

  const schema& operator()(const schema_ref& r) {
    assert(!r.is_fundamental());
    return *schemas.find(r);
  }
};

static void flatten_not(schema& this_schema, not_schema& n, access_schema get, size_t& changes,
                        std::unordered_map<schema_ref, schema_ref>& replace_list) {
  // здесь можно теоретически складывать даже с доп требованиями, но для этого нужно инвертировать
  // требования и создавать типы not<X>
  if (this_schema.has_requirements())
    return;
  if (std::optional f = n.type.if_fundamental()) {
    if (*f == fundamental_e::never)
      replace_list[this_schema.ref()] = make_fundamental_ref(fundamental_e::any);  // not<@never@> -> @any@
    else if (*f == fundamental_e::any)
      replace_list[this_schema.ref()] = make_fundamental_ref(fundamental_e::never);  // not<@any@> -> @never@
    return;
  }
  auto& ot = get(n.type);
  if (ot.has_requirements())
    return;
  if (auto* f = ot.type.if_not()) {
    replace_list[this_schema.ref()] = f->type;  // not<not<T>> -> T
    ++changes;
  } else if (ot.type.is_fundamental()) {
  }
}

// `this_schema` - схема в которой лежит оптимизируемый allOf
static void flatten_allof(schema& this_schema, allof_schema& allof, access_schema get, size_t& changes,
                          std::unordered_map<schema_ref, schema_ref>& replace_list) {
  auto isallof = [&](schema_ref& s) {
    if (s.is_fundamental())
      return false;
    return get(s).type.is_allof();
  };
  array<schema_ref> inlined(std::from_range, allof.schemas | std::views::filter(isallof));
  array<schema_ref> inlined_schemas;
  for (schema_ref& ref : inlined) {
    // требования складываются для allOf
    auto& t = get(ref);
    if (t.required)
      this_schema.add_requirements(*t.required);
    inlined_schemas.append_range(t.type.if_allof()->schemas);
  }
  if (!inlined.empty())
    ++changes;
  std::erase_if(allof.schemas, isallof);
  allof.schemas.append_range(std::move(inlined_schemas));
  remove_duplicates(allof.schemas);
  if (allof.schemas.empty() && !this_schema.has_requirements()) {
    // allof<> -> any (также как (A && ...) == true если A пусто)
    this_schema.type.data = fundamental_e::any;
    ++changes;
  } else if (allof.schemas.size() == 1 && !this_schema.has_requirements()) {
    replace_list[this_schema.ref()] = allof.schemas.front();  // allof<X> -> X
    ++changes;
  }
}

static void flatten_anyof(schema& this_schema, anyof_schema& anyof, access_schema get, size_t& changes,
                          std::unordered_map<schema_ref, schema_ref>& replace_list) {
  auto isanyof = [&](schema_ref& s) {
    if (s.is_fundamental())
      return false;
    auto& t = get(s);
    // anyof с требованиями уже нетривиальный тип, который так просто не заинлайнишь
    return t.type.is_anyof() && !t.has_requirements();
  };
  array<schema_ref> inlined(std::from_range, anyof.schemas | std::views::filter(isanyof));
  array<schema_ref> inlined_schemas;
  for (schema_ref& ref : inlined) {
    auto& t = get(ref);
    assert(!t.has_requirements());
    inlined_schemas.append_range(t.type.if_anyof()->schemas);
  }
  if (!inlined.empty())
    ++changes;
  std::erase_if(anyof.schemas, isanyof);
  anyof.schemas.append_range(std::move(inlined_schemas));
  remove_duplicates(anyof.schemas);
  if (anyof.schemas.empty() && !this_schema.has_requirements()) {
    // anyof<> -> never (также как (A || ...) == false если A пусто)
    this_schema.type.data = fundamental_e::never;
    ++changes;
  } else if (anyof.schemas.size() == 1 && !this_schema.has_requirements()) {
    replace_list[this_schema.ref()] = anyof.schemas.front();  // anyof<X> -> X
    ++changes;
  }
}

static void flatten_oneof(schema& this_schema, oneof_schema& oneof, access_schema get, size_t& changes,
                          std::unordered_map<schema_ref, schema_ref>& replace_list) {
  auto isoneof = [&](schema_ref& s) {
    if (s.is_fundamental())
      return false;
    auto& t = get(s);
    // oneof с требованиями уже нетривиальный тип, который так просто не заинлайнишь
    return t.type.is_oneof() && !t.has_requirements();
  };
  array<schema_ref> inlined(std::from_range, oneof.schemas | std::views::filter(isoneof));
  array<schema_ref> inlined_schemas;
  for (schema_ref& ref : inlined) {
    auto& t = get(ref);
    assert(!t.has_requirements());
    inlined_schemas.append_range(t.type.if_oneof()->schemas);
  }
  if (!inlined.empty())
    ++changes;
  std::erase_if(oneof.schemas, isoneof);
  oneof.schemas.append_range(std::move(inlined_schemas));
  remove_duplicates(oneof.schemas);
  if (oneof.schemas.empty() && !this_schema.has_requirements()) {
    // oneof<> -> never (также как (A || ...) == false если A пусто)
    this_schema.type.data = fundamental_e::never;
    ++changes;
  } else if (oneof.schemas.size() == 1 && !this_schema.has_requirements()) {
    replace_list[this_schema.ref()] = oneof.schemas.front();  // oneof<X> -> X
    ++changes;
  }
}

// TODO тест кейс когда в результате получется фундаментальная ссылка - в таком случае её нужно убрать
// либо поставить этот пасс до, либо наоборот в этом пасе переиспользовать убирание фундаментальной ссылки
// лучше наверное пасс поставить ещё раз после этого. И на убирание более неиспользуемых тоже
// TODO optimization вынести в отдельную функцию, там должны быть все пасы с учётом опций (типа включены
// оптимизации или нет, включены ли конкретные убирающие неиспользуемые и тд
//
// TODO тест кейс на not<not<T>> -> T
// и not<not<not<T>>> -> not<T> и тд
static void flatten_combinator(const schema& s, const schemas_t& all_schemas, size_t& changes,
                               std::unordered_map<schema_ref, schema_ref>& replace_list) {
  // тип не влияет на хеш и сравнение
  auto& this_schema = const_cast<schema&>(s);
  access_schema accessor(all_schemas);
  matcher vtor{
      [&](not_schema& n) { flatten_not(this_schema, n, accessor, changes, replace_list); },
      [](array_schema& arr) {},
      [](object_schema&) {},
      [](enum_schema&) {},
      [](fundamental_e&) {},
      [&](allof_schema& af) { flatten_allof(this_schema, af, accessor, changes, replace_list); },
      [&](anyof_schema& anf) { flatten_anyof(this_schema, anf, accessor, changes, replace_list); },
      [&](oneof_schema& oof) { flatten_oneof(this_schema, oof, accessor, changes, replace_list); },
  };
  std::visit(vtor, this_schema.type.data);
}

ir_object pass_combinators_flatten(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return ir;
  std::unordered_map<schema_ref, schema_ref> replace_list;
  for (;;) {
    size_t changes = 0;
    for (const schema& s : *ir.schemas)
      flatten_combinator(s, *ir.schemas, changes, replace_list);
    if (changes == 0 && replace_list.empty())
      break;
    // заменяем все использования типов в replace_list и удаляем их
    auto vtor = [&](schema_ref& r) {
      auto it = replace_list.find(r);
      if (it != replace_list.end())
        r = it->second;
    };
    schema_ref_vtor replacer(vtor);
    replacer.visit(ir);
    for (auto&& [from, to] : replace_list)
      ir.schemas->erase(from);
    replace_list.clear();
  }
  return ir;
}
// TODO
#if 0
ir_object pass_remove_useless_requirements(ir_object ir, const ir_options& options) {
  if (!ir.schemas)
    return ir;
  access_schema get(*ir.schemas);
  // TODO if never то все остальные бесполезны
  // TODO для каждого в пачке удалить всех кого он перекрыл
  // never покрывает всех, allof покрывает все allof подмножества (так то здесь allof можно тоже объединять)
  // anyof с меньшим числом аргументов перекрывает другой anyof, перекрывающиеся oneof образуют oneof
  // пересечение
  // all проигрывает всем и вылетает
  auto is_useless = [&](schema_ref& req) {
    if (std::optional f = req.if_fundamental())
      return *f == fundamental_e::any;
  };
  for (const schema& _s : *ir.schemas) {
    // требования не влияют на кеширование
    schema& s = const_cast<schema&>(_s);
    if (s.required) {
      auto removed = std::ranges::remove_if(*s.required, is_useless);
    }
  }
  // TODO проходим по всем типам в IR, если требования с @any@/not<@never@> и тд (которые могли получиться
  // ранее)
  return ir;
}
#endif

}  // namespace svarg::ir
