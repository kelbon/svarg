
#include "svarg/ir/optimization_passes.hpp"
#define program_options_file "svarg/cli/ir_options.def"
#include <clinok/cli_interface.hpp>

#include <svarg/frontend/openapi/generate_ir.hpp>
#include <print>

enum struct format_e {
  yaml,
  json,
};

using pass_ptr = decltype(&svarg::ir::pass_combinators_flatten);

// clang-format off
inline const std::unordered_map<std::string_view, pass_ptr> passes_map{
  {"rts", svarg::ir::pass_rm_trivial_schemas},
  {"rus", svarg::ir::pass_rm_unused_schemas},
  {"cf", svarg::ir::pass_combinators_flatten},
  {"rur",svarg::ir::pass_rm_useless_requirements},
  {"ta",svarg::ir::pass_transform_allofs},
};
// clang-format on

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
      std::string_view pass_name = std::string_view(v);
      if (!passes_map.contains(pass_name)) {
        std::println("invalid passes stirng `{}`", cliopts.passes);
        return -1;
      }
      ir = passes_map.at(pass_name)(std::move(ir), options.ir_opts).ir;
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
