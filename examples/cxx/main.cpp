#include <iostream>

#include "generated/api/client.hpp"

int main() {
  // The exact symbols svarg generates for api.yaml aren't shown here -
  // adjust this to whatever it actually produces (handler base classes,
  // request/response types, etc.). This just proves the generated header
  // is visible on the include path and the target links correctly.
  std::cout << "svarg_simple_cxx_example: generated header included OK\n";
  return 0;
}
