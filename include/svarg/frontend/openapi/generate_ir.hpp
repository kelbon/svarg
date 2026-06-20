#pragma once

#include "svarg/ir/ir_object.hpp"
#include "svarg/frontend/openapi/openapi.hpp"

/*

Note: на уровне IR важны только .filename() спек, т.е. две спеки по разным путям с одинаковым именем обязаны
совпадать по содержимому
Это важно для генерации с дополнительными инклуд директориями - генерация даст один и тот же результат в одни
и те же файлы что и нужно по логике (дабы не дублировать типы)

*/
namespace svarg {

// просто переводит в IR без каких-либо оптимизирующих пассов
[[nodiscard]] ir::ir_object openapi2ir_raw(fs::path specsdir, std::vector<fs::path> specs2generate,
                                           openapi_options options = {});

// переводит в IR и оптимизирует
[[nodiscard]] ir::ir_object openapi2ir(fs::path specsdir, std::vector<fs::path> specs2generate,
                                       openapi_options options = {});

}  // namespace svarg
