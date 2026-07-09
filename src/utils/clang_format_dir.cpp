#include "svarg/utils/clang_format_dir.hpp"

#include <strswitch/strswitch.hpp>

#include <boost/process.hpp>

namespace svarg {

[[nodiscard]] static bool is_c_file_extension(std::string_view extension) {
  return ss::string_switch<bool>(extension)
      .cases(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", true)
      .or_default(false);
}

void clang_format_dir(const fs::path& dir, fn_ref<void(const fs::path&, bool /*success*/)> f) {
  if (!fs::exists(dir) || !fs::is_directory(dir))
    return;

  auto clang_format = boost::process::search_path("clang-format");
  if (clang_format.empty())
    return;

  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    if (!is_c_file_extension(entry.path().extension().string()))
      continue;
    int result = boost::process::system(clang_format, "-i", entry.path().string());
    f(entry.path(), result == 0);
  }
}

}  // namespace svarg
