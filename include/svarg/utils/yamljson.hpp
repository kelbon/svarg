#pragma once

#include <string_view>
#include <string>
#include <filesystem>

#include "svarg/utils/fn_ref.hpp"

namespace svarg {

namespace fs = std::filesystem;

// можно подавать и json тоже, внезапно.
// так как json это подмножество yaml
std::string yaml2json(std::string_view yaml);
std::string json2yaml(std::string_view json);
std::string load_yaml_as_json(fs::path yaml);

[[nodiscard]] bool is_yaml_extension(std::string_view extension) noexcept;
[[nodiscard]] bool is_json_extension(std::string_view extension) noexcept;

[[nodiscard]] inline bool is_yaml_or_json_extension(std::string_view extension) noexcept {
  return is_yaml_extension(extension) || is_json_extension(extension);
}

// передаёт в `fn` содержимое файла как json, даже если это yaml
void pass_as_json(fs::path path, fn_cref<void(std::string_view) const> fn);

// пишет в файл. Если его нет - создаёт (не включая директории), если он есть - перезаписывает
void write_into(const fs::path&, std::string_view);

}  // namespace svarg
