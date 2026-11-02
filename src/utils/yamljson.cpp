#include "svarg/utils/yamljson.hpp"

#include <boost/iostreams/device/mapped_file.hpp>
#include <ryml.hpp>
// не спрашивайте... ryml гениален. ПОРЯДОК ХЕДЕРОВ ВАЖЕН
#include <ryml_std.hpp>

#include <format>
#include <fstream>
#include <strswitch/strswitch.hpp>

namespace svarg {

bool is_yaml_extension(std::string_view extension) noexcept {
  return ss::string_switch<bool>(extension).cases(".yaml", ".yml", true).or_default(false);
}

bool is_json_extension(std::string_view extension) noexcept {
  return ss::string_switch<bool>(extension).cases(".json", ".jsonc", ".json5", true).or_default(false);
}

void pass_as_json(fs::path path, fn_cref<void(std::string_view) const> fn) {
  fs::path ext = path.extension();
  if (is_yaml_extension(ext.string())) {
    std::string json = load_yaml_as_json(path);
    return fn(std::string_view(json));
  } else if (is_json_extension(ext.string())) {
    boost::iostreams::mapped_file_source file(path.string());
    return fn(std::string_view(file.data(), file.size()));
  } else {
    throw std::invalid_argument(
        std::format("invalid extension of `{}`, allowed extensions .json and .yaml", path.string()));
  }
}

std::string yaml2json(std::string_view yaml) {
  ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(yaml.data(), yaml.size()));
  ryml::id_type root = tree.root_id();
  // файл, начинающийся с `---`, rapidyaml разбирает как поток документов
  if (tree.is_stream(root)) {
    if (tree.num_children(root) != 1)
      throw std::invalid_argument(
          std::format("expected one YAML document, got {}", size_t(tree.num_children(root))));
    root = tree.first_child(root);
  }
  return ryml::emitrs_json<std::string>(tree, root);
}

inline void force_block_style(ryml::NodeRef node) {
  if (node.is_map() || node.is_seq()) {
    node.set_container_style(ryml::BLOCK);
    for (ryml::NodeRef child : node.children()) {
      force_block_style(child);
    }
  }
}
std::string json2yaml(std::string_view json) {
  ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(json.data(), json.size()));
  force_block_style(tree.rootref());
  return ryml::emitrs_yaml<std::string>(tree);
}

std::string load_yaml_as_json(fs::path yaml) {
  boost::iostreams::mapped_file_source file(yaml.string());
  return yaml2json(std::string_view(file.data(), file.size()));
}

void write_into(const fs::path& path, std::string_view content) {
  std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!file)
    throw std::runtime_error("write_into: failed to open file: " + path.generic_string());
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!file)
    throw std::runtime_error("write_into: failed to write to file: " + path.generic_string());
}

}  // namespace svarg
