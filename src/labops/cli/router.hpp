#pragma once

namespace labops::cli {

// Routes `labops` subcommands and returns process exit codes with a stable
// contract for scripts and CI:
//   0 => success
//   1 => command failed after valid invocation
//   2 => usage error (unknown command / invalid args)
int Dispatch(int argc, char** argv);

} // namespace labops::cli
