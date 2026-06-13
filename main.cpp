
#define program_options_file <svarg/driver/program_options.def>
#include <clinok/cli_interface.hpp>

// TODO driver - запускает с правильными аргументами фронтенд и бекенд.
// На вход вероятно подаётся фронтенд и бекенд (имена программ, интерфейс их унифицирован?)
// и есть дефолт - опенапи -> IR -> python jinja backend
int main(int argc, char* argv[]) {
  cli::options opt = cli::parse_or_exit(argc, argv);
  return 0;
}
