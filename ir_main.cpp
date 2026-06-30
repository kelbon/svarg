
#include "svarg/ir/optimization_passes.hpp"
#define program_options_file "svarg/cli/ir_options.def"
#include <clinok/cli_interface.hpp>

#include <svarg/frontend/openapi/generate_ir.hpp>
#include <print>
#include <fstream>

enum struct format_e {
  yaml,
  json,
};

enum struct passes_e {
  rts,
  rus,
  cf,
  rur,
  // rts - remove trivial schemas, rus - remove unused schemas, cf - combinators flatten, rur - remove useless
  // requirements
};

using namespace svarg;

int main(int argc, char* argv[]) try {
  cli::options cliopts = cli::parse_or_exit(argc, argv);
  openapi_options options;

  options.ir_opts.enable_optimizations = cliopts.optimize;
  std::optional f = ss::string_switch<format_e>(cliopts.out_format)
                        .case_("yaml", format_e::yaml)
                        .case_("json", format_e::json)
                        .or_nullopt();
  if (!f) {
    std::println("invalid format, valid are json or yaml");
    return -1;
  }
  fs::path out;
  if (cliopts.out.empty()) {
    std::string out_str = "a";

    if (std::filesystem::exists(std::format("{}.{}", out_str, cliopts.out_format))) {
      size_t i = 1;
      while (fs::exists(std::format("{}{}.{}", out_str, i, cliopts.out_format)))
        ++i;
      out = std::format("{}{}.{}", out_str, i, cliopts.out_format);
    } else {
      out = out_str;
    }
  } else {
    out = cliopts.out;
    if (auto e = out.extension(); !e.empty() && e != std::format(".{}", cliopts.out_format)) {
      std::println("invalid output file extension, expected: {}, actual {}", cliopts.out_format,
                   e.generic_string());
      return -1;
    }
  }

  if (cliopts.additional_args.empty()) {
    std::println("no one spec file not provided, expected atleast one");
    return -1;
  }
  std::vector<fs::path> inputs;
  for (std::string_view arg : cliopts.additional_args) {
    fs::path p = arg;
    if (!fs::exists(p)) {
      std::println("spec file {} not found", arg);
      return -1;
    }
    inputs.push_back(std::move(p));
  }
  ir::ir_object ir = openapi2ir(std::filesystem::current_path(), std::move(inputs), options);
  if (!cliopts.passes.empty()) {
    for (auto v : cliopts.passes | std::views::split(' ')) {
      if (v.empty())
        continue;
      std::optional p = ss::string_switch<passes_e>(std::string_view(v))
                            .case_("rts", passes_e::rts)
                            .case_("rus", passes_e::rus)
                            .case_("cf", passes_e::cf)
                            .case_("rur", passes_e::rur)
                            .or_nullopt();
      if (!p) {
        std::println("invalid passes stirng `{}`", cliopts.passes);
        return -1;
      }
      switch (*p) {
        case passes_e::rts:
          ir = ir::pass_rm_trivial_schemas(std::move(ir), options.ir_opts).ir;
          break;
        case passes_e::rus:
          ir = ir::pass_rm_unused_schemas(std::move(ir), options.ir_opts).ir;
          break;
        case passes_e::cf:
          ir = ir::pass_combinators_flatten(std::move(ir), options.ir_opts).ir;
          break;
        case passes_e::rur:
          ir = ir::pass_rm_useless_requirements(std::move(ir), options.ir_opts).ir;
          break;
      }
    }
  }
  if (*f == format_e::yaml)
    write_into(out, to_yaml_string(ir));
  else
    write_into(out, to_json_string(ir));
  std::println("output written to {}", fs::absolute(out).generic_string());
  return 0;
} catch (std::exception& e) {
  std::println("exception: {}", e.what());
  return -1;
}
