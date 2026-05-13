
#define program_options_file <svarg/program_options.def>
#include <clinok/cli_interface.hpp>

int main(int argc, char* argv[]) {
  cli::options opt = cli::parse_or_exit(argc, argv);
  return 0;
}
