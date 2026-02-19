#include "backends/webcam/webcam_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(const std::string& text, std::string_view expected) {
  if (text.find(expected) == std::string::npos) {
    Fail("expected substring not found");
  }
}

} // namespace

int main() {
  labops::backends::webcam::WebcamBackend backend;

  std::string error;
  if (!backend.SetParam("device.index", "0", error)) {
    Fail("set_param failed unexpectedly");
  }

  const labops::backends::BackendConfig config = backend.DumpConfig();
  if (config.find("backend") == config.end() || config.at("backend") != "webcam") {
    Fail("dump_config missing backend=webcam");
  }
  if (config.find("device.index") == config.end() || config.at("device.index") != "0") {
    Fail("dump_config missing echoed parameter");
  }

  if (backend.Connect(error)) {
    Fail("connect unexpectedly succeeded");
  }
  AssertContains(error, "BACKEND_NOT_AVAILABLE");

  std::cout << "webcam_backend_smoke: ok\n";
  return 0;
}
