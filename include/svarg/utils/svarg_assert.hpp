#pragma once

#include <cassert>

// Дорогие ассёрты выключены по умолчанию даже в дебаге
#define SVARG_ENABLE_ASSERTS 0

#if SVARG_ENABLE_ASSERTS
  #define SVARG_ASSERT(...) assert((__VA_ARGS__))
#else
  #define SVARG_ASSERT(...) static_cast<void>(sizeof(static_cast<bool>(__VA_ARGS__)))
#endif
