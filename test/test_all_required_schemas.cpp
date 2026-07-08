#include "svarg/backend/cxx/support/validate_base.hpp"

#include <moko3/moko3.hpp>

using svarg::cxx_support::all_required_schemas_t;
using svarg::cxx_support::has_recursive_requirements;
using svarg::cxx_support::is_recursive_requirement;

TEST("all required schemas") {
  struct A {
    // self reference
    using required_schemas = metel::typelist<int, A>;
  };
  static_assert(std::is_same_v<all_required_schemas_t<A>, metel::typelist<int>>);
  static_assert(is_recursive_requirement<A, A>());
  static_assert(!is_recursive_requirement<int, A>());
  static_assert(has_recursive_requirements<A>());
  struct C;
  struct B {
    // взаимная рекурсия
    using required_schemas = metel::typelist<C>;
  };
  static_assert(std::is_same_v<all_required_schemas_t<B>, metel::typelist<C>>);
  struct C {
    using required_schemas = metel::typelist<B>;
  };

  static_assert(std::is_same_v<all_required_schemas_t<C>, metel::typelist<B>>);
  static_assert(is_recursive_requirement<C, B>());
  static_assert(is_recursive_requirement<B, C>());
  static_assert(!is_recursive_requirement<C, A>());
  static_assert(has_recursive_requirements<B>());
  static_assert(has_recursive_requirements<C>());
  static_assert(!has_recursive_requirements<int>());

  struct D {
    // зависимость на рекурсивный тип
    using required_schemas = metel::typelist<int, A>;
  };

  static_assert(std::is_same_v<all_required_schemas_t<D>, metel::typelist<int, A>>);
  // на первом уровне рекурсии нет, но если пойдёт проверять A - тогда уже проблема обнаружится
  static_assert(!has_recursive_requirements<D>());

  struct E {
    // нет зависимостей
    using required_schemas = metel::typelist<>;
  };
  static_assert(std::is_same_v<all_required_schemas_t<E>, metel::typelist<>>);

  static_assert(!has_recursive_requirements<E>());
  // вообще нет required_schemas
  struct J {};

  static_assert(std::is_same_v<all_required_schemas_t<J>, metel::typelist<>>);
  static_assert(!has_recursive_requirements<J>());
}

MOKO3_MAIN;
