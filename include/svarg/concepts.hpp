#pragma once

#include <type_traits>

namespace svarg {

template <typename T>
concept aggregate = std::is_aggregate_v<std::decay_t<T>>;

// маркер, чтобы точно отделить генерируемые типы при парсинге и сериализации от фронтенд/бекенд типов
#define SVARG_FRONTEND_TYPE using svarg_frontend_type = int

#define SVARG_IR_TYPE using svarg_ir_type = int

template <typename T>
concept frontend_type = requires { typename T::svarg_frontend_type; };

template <typename T>
concept ir_type = requires { typename T::svarg_ir_type; };

template <typename T>
concept front_or_ir_type = frontend_type<T> || ir_type<T>;

}  // namespace svarg
