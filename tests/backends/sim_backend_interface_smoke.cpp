#include "backends/camera_backend.hpp"
#include "backends/sim/sim_camera_backend.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

} // namespace

int main() {
  std::unique_ptr<labops::backends::ICameraBackend> backend =
      std::make_unique<labops::backends::sim::SimCameraBackend>();

  std::string error;
  if (!backend->Connect(error)) {
    Fail("connect failed: " + error);
  }

  if (!backend->SetParam("fps", "50", error)) {
    Fail("set_param failed: " + error);
  }

  if (!backend->Start(error)) {
    Fail("start failed: " + error);
  }

  const std::vector<labops::backends::FrameSample> frames =
      backend->PullFrames(std::chrono::milliseconds(200), error);
  if (!error.empty()) {
    Fail("pull_frames returned error: " + error);
  }

  if (frames.size() != 10U) {
    Fail("pull_frames frame count mismatch");
  }

  const labops::backends::BackendConfig config = backend->DumpConfig();
  if (config.find("backend") == config.end() || config.at("backend") != "sim") {
    Fail("dump_config missing backend=sim");
  }
  if (config.find("fps") == config.end() || config.at("fps") != "50") {
    Fail("dump_config missing fps=50");
  }
  if (config.find("running") == config.end() || config.at("running") != "true") {
    Fail("dump_config missing running=true");
  }

  if (!backend->Stop(error)) {
    Fail("stop failed: " + error);
  }

  std::cout << "sim_backend_interface_smoke: ok\n";
  return 0;
}
