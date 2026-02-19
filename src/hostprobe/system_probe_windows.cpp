#include "hostprobe/system_probe_internal.hpp"

#if defined(_WIN32)

#define NOMINMAX
#include <windows.h>

namespace labops::hostprobe::internal {

void AddSystemHostnameTokensPlatform(std::vector<std::string>& out) {
  char name[256] = {};
  DWORD size = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
  if (GetComputerNameA(name, &size) != 0 && size > 0U) {
    AddIdentifierTokenAndVariants(out, std::string(name, size));
  }
}

std::string DetectOsVersionPlatform() {
  return "unknown";
}

std::string ProbeCpuModelPlatform() {
  return "unknown";
}

std::uint64_t ProbeRamTotalBytesPlatform() {
  MEMORYSTATUSEX memory_status{};
  memory_status.dwLength = sizeof(memory_status);
  if (GlobalMemoryStatusEx(&memory_status) != 0) {
    return static_cast<std::uint64_t>(memory_status.ullTotalPhys);
  }
  return 0;
}

std::uint64_t ProbeUptimeSecondsPlatform() {
  return static_cast<std::uint64_t>(GetTickCount64() / 1000ULL);
}

std::array<std::optional<double>, 3> ProbeLoadAveragesPlatform() {
  return {};
}

void CollectNicProbePlatform(NicProbeSnapshot& snapshot) {
  NicCommandCapture ipconfig_all = CaptureCommand("nic_ipconfig_all.txt", "ipconfig /all");
  ParseWindowsIpconfigOutput(ipconfig_all.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ipconfig_all));
  SortInterfaces(snapshot.highlights);
}

} // namespace labops::hostprobe::internal

#endif
