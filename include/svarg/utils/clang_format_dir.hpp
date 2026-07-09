#pragma once

#include "svarg/utils/fn_ref.hpp"
#include "svarg/utils/yamljson.hpp"  // IWYU pragma: keep

namespace svarg {

// запускает clang-format на всех файлах директории рекурсивно
// если их расширение похоже на C или C++ файлы
// если clang-format нет или директория не существует, не делает ничего
// для каждого отформатированного файла вызывает 'f'
void clang_format_dir(const fs::path& dir, fn_ref<void(const fs::path&, bool /*success*/)> f);

}  // namespace svarg
