
#include "svarg/ir/optimization_passes.hpp"
#include "svarg/utils/clang_format_dir.hpp"
#define CLINOK_NAMESPACE_NAME cxxcli
#define program_options_file "svarg/cli/cxx_options.def"
#include <clinok/cli_interface.hpp>

#include <svarg/ir/ir_object.hpp>
#include <svarg/backend/cxx/render.hpp>

#include <print>

using namespace svarg;

int main(int argc, char* argv[]) try {
  cxxcli::options cliopts = cxxcli::parse_or_exit(argc, argv);

  std::vector<fs::path> inputs;
  for (fs::path p : cliopts.additional_args) {
    if (!fs::exists(p)) {
      std::println("IR file {} not found", fs::absolute(p).generic_string());
      return -1;
    }
    if (p.extension() != ".yaml" && p.extension() != ".yml")
      std::println("warning: IR file extension is not yaml");
    inputs.push_back(std::move(p));
  }
  ir::ir_object ir;
  for (fs::path p : cliopts.additional_args)
    ir.merge(from_json<ir::ir_object>(load_yaml_as_json(p)));
  if (cliopts.ignore_requirements && ir.schemas) {
    for (const ir::schema& s : *ir.schemas) {
      const_cast<ir::schema&>(s).required = {};
    }
  }
  if (cliopts.optimize || cliopts.ignore_requirements)
    ir = ir::optimization_pipeline(std::move(ir), ir::ir_options{});
  cxx::render_options options;
  options.simple_cxx_identifiers = cliopts.simple_identifiers;
  cxx::renderer r(ir, options);
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
