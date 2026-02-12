#include "labops/cli/router.hpp"

int main(int argc, char** argv) {
  // Keep the process entrypoint intentionally thin. All command parsing and
  // output/exit-code contracts live in the CLI router.
  return labops::cli::Dispatch(argc, argv);
}
