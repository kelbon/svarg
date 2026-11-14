#pragma once

#include <kelcoro/task.hpp>

namespace test_task_ctx {

// контекст сгенерированных задач: считает созданные
struct ctx : dd::null_context {
  static inline int created = 0;

  void on_create_location(dd::source_location) noexcept {
    ++created;
  }
};

}  // namespace test_task_ctx
