
#include <filesystem>

#include "svarg/frontend/openapi/generate_ir.hpp"
#define CLINOK_NAMESPACE_NAME svarg_cli
#define program_options_file <svarg/cli/cxx_driver_options.def>
#include <clinok/cli_interface.hpp>

#include "svarg/ir/optimization_passes.hpp"
#include "svarg/utils/clang_format_dir.hpp"

#include <svarg/ir/ir_object.hpp>
#include <svarg/backend/cxx/render.hpp>

#include <print>

using namespace svarg;
[[nodiscard]] static bool is_ws(char c) {
  return c == ' ' || c == '\n';
}

[[nodiscard]] static std::string monomorph_mask(std::string_view s) {
  while (!s.empty() && is_ws(s.front()))
    s.remove_prefix(1);
  while (!s.empty() && is_ws(s.back()))
    s.remove_suffix(1);
  std::string res(s);
  for (char& c : res)
    if (c == '\\')
      c = '/';
  return res;
}

[[nodiscard]] static bool is_mask(std::string_view s) {
  return monomorph_mask(s).ends_with("/*");
}

[[nodiscard]] static bool match_mask(const std::filesystem::path& p, std::string_view mask) {
  assert(is_mask(mask));
  std::string m = monomorph_mask(mask);
  // убираем /*
  m.pop_back();
  m.pop_back();
  return p.generic_string().starts_with(m);
}

// возвращает set для одинакового порядка генерации. Все пути относительно specs_dir
static std::set<fs::path> collect_input_files(fs::path specs_dir,
                                              std::span<const std::string_view> spec_paths) {
  if (!fs::exists(specs_dir) || !fs::is_directory(specs_dir))
    throw validate_error("spec directory `{}` does not exists", specs_dir.generic_string());
  std::set<fs::path> not_masks;
  for (std::string_view spec : spec_paths) {
    if (!is_mask(spec))
      not_masks.insert(spec);
  }
  std::set<std::string> masks;
  for (std::string_view spec : spec_paths) {
    if (is_mask(spec))
      masks.insert(monomorph_mask(spec));
  }

  std::set<fs::path> inputs;
  for (fs::path p : not_masks) {
    if (!p.is_absolute())
      p = specs_dir / p;
    if (!fs::exists(p))
      throw validate_error("spec file `{}` not found", p.generic_string());
    inputs.insert(fs::relative(p, specs_dir));
  }

  for (auto&& entry : fs::recursive_directory_iterator(specs_dir)) {
    if (entry.is_regular_file()) {
      bool match_any_mask = [&] {
        for (auto& m : masks)
          if (match_mask(entry.path(), m))
            return true;
        return false;
      }();
      if (match_any_mask)
        inputs.insert(fs::relative(entry.path(), specs_dir));
    }
  }

  return inputs;
}

int main(int argc, char* argv[]) try {
  svarg_cli::options cliopts = svarg_cli::parse_or_exit(argc, argv);

  std::set<fs::path> inputs = collect_input_files(cliopts.specs_dir, cliopts.additional_args);
  for (const auto& p : inputs) {
    if (!is_yaml_or_json_extension(p.extension().string()))
      std::println("warning: openapi spec file extension is not yaml or json");
  }

  std::vector<fs::path> specs2generate(inputs.begin(), inputs.end());
  openapi_options options;
  options.unsetted_additional_properties = cliopts.unsetted_additional_properties;
  for (auto&& inc : cliopts.include_dirs.args) {
    if (!fs::exists(inc) || !fs::is_directory(inc)) {
      std::println("include directory `{}` does not exists", inc);
      return -1;
    }
    options.include_dirs.push_back(inc);
  }

  switch (cliopts.allow_remove) {
    case svarg_cli::allow_remove_e::noone:
      options.ir_opts.allow_rm_unused = svarg::ir::remove_category_e::noone;
      break;
    case svarg_cli::allow_remove_e::only_inline_schemas:
      options.ir_opts.allow_rm_unused = svarg::ir::remove_category_e::only_inline_schemas;
      break;
    case svarg_cli::allow_remove_e::any:
      options.ir_opts.allow_rm_unused = svarg::ir::remove_category_e::any;
      break;
  }
  options.ir_opts.enable_optimizations = cliopts.optimize;
  // options.ir_opts.marked_used;
  options.resolve_url_refs = cliopts.allow_network_refs;
  options.ref_max_depth = cliopts.references_max_recursion_depth;

  ir::ir_object ir = openapi2ir(cliopts.specs_dir, specs2generate, options);
  if (cliopts.ignore_requirements && ir.schemas) {
    for (const ir::schema& s : *ir.schemas) {
      const_cast<ir::schema&>(s).required = {};
    }
    if (cliopts.optimize)
      ir = ir::optimization_pipeline(std::move(ir), options.ir_opts);
  }

  cxx::render_options renoptions;
  renoptions.simple_cxx_identifiers = cliopts.simple_identifiers;
  cxx::renderer r(ir, renoptions);
  r.render2cxx(cliopts.out);
  auto onfile = [](const fs::path& p, bool success) {
    if (success)
      std::println("file `{}` formatted", p.generic_string());
    else
      std::println("cannot format file `{}", p.generic_string());
  };
  clang_format_dir(cliopts.out, onfile);
  std::println("output written to {} directory", fs::absolute(cliopts.out).generic_string());
  return 0;
} catch (std::exception& e) {
  std::println("exception: {}", e.what());
  return -1;
}
