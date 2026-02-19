#include "hostprobe/system_probe_internal.hpp"

#if defined(__linux__)

#include <fstream>
#include <sstream>
#include <string_view>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace labops::hostprobe::internal {

void AddSystemHostnameTokensPlatform(std::vector<std::string>& out) {
  char name[256] = {};
  if (gethostname(name, sizeof(name)) == 0) {
    name[sizeof(name) - 1U] = '\0';
    AddIdentifierTokenAndVariants(out, std::string(name));
  }
}

std::string DetectOsVersionPlatform() {
  struct utsname uts{};
  if (uname(&uts) == 0) {
    return std::string(uts.release);
  }
  return "unknown";
}

std::string ProbeCpuModelPlatform() {
  std::ifstream input("/proc/cpuinfo");
  std::string line;
  constexpr std::string_view kPrefix = "model name";
  while (std::getline(input, line)) {
    if (line.rfind(kPrefix, 0) == 0U) {
      const std::size_t colon = line.find(':');
      if (colon == std::string::npos || colon + 1U >= line.size()) {
        continue;
      }
      std::string value = line.substr(colon + 1U);
      const std::size_t first = value.find_first_not_of(" \t");
      if (first == std::string::npos) {
        continue;
      }
      const std::size_t last = value.find_last_not_of(" \t");
      return value.substr(first, last - first + 1U);
    }
  }
  return "unknown";
}

std::uint64_t ProbeRamTotalBytesPlatform() {
  struct sysinfo info{};
  if (sysinfo(&info) == 0) {
    return static_cast<std::uint64_t>(info.totalram) * static_cast<std::uint64_t>(info.mem_unit);
  }
  return 0;
}

std::uint64_t ProbeUptimeSecondsPlatform() {
  struct sysinfo info{};
  if (sysinfo(&info) == 0 && info.uptime >= 0) {
    return static_cast<std::uint64_t>(info.uptime);
  }
  return 0;
}

std::array<std::optional<double>, 3> ProbeLoadAveragesPlatform() {
  std::array<std::optional<double>, 3> values;
  double loads[3] = {0.0, 0.0, 0.0};
  if (getloadavg(loads, 3) == 3) {
    values[0] = loads[0];
    values[1] = loads[1];
    values[2] = loads[2];
  }
  return values;
}

void CollectNicProbePlatform(NicProbeSnapshot& snapshot) {
  NicCommandCapture ip_a = CaptureCommand("nic_ip_a.txt", "ip a");
  ParseLinuxIpAddressOutput(ip_a.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ip_a));

  NicCommandCapture ip_r = CaptureCommand("nic_ip_r.txt", "ip r");
  ParseLinuxRouteOutput(ip_r.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ip_r));

  NicCommandCapture ethtool_capture;
  ethtool_capture.file_name = "nic_ethtool.txt";
  ethtool_capture.command = "ethtool <interface>";

  if (!IsCommandAvailable("ethtool")) {
    ethtool_capture.command_available = false;
    ethtool_capture.exit_code = 127;
    ethtool_capture.output = "ethtool not available on host PATH.\n";
    snapshot.raw_captures.push_back(std::move(ethtool_capture));
    SortInterfaces(snapshot.highlights);
    return;
  }

  std::vector<std::string> interface_names;
  interface_names.reserve(snapshot.highlights.interfaces.size());
  for (const auto& iface : snapshot.highlights.interfaces) {
    if (iface.name.empty() || iface.name == "lo") {
      continue;
    }
    interface_names.push_back(iface.name);
  }

  if (interface_names.empty()) {
    interface_names.push_back("eth0");
  }

  int aggregate_exit_code = 0;
  std::ostringstream aggregate;
  for (const auto& iface_name : interface_names) {
    const std::string command = "ethtool " + iface_name;
    NicCommandCapture per_iface = CaptureCommand("", command);
    NicInterfaceHighlight& iface = GetOrCreateInterface(snapshot.highlights, iface_name);
    if (const auto speed = ParseLinuxEthtoolSpeedHint(per_iface.output); speed.has_value()) {
      iface.link_speed_hint = speed.value();
    }
    if (per_iface.exit_code != 0) {
      aggregate_exit_code = per_iface.exit_code;
    }

    aggregate << "# command: " << command << "\n";
    aggregate << "# exit_code: " << per_iface.exit_code << "\n\n";
    aggregate << per_iface.output;
    if (!per_iface.output.empty() && per_iface.output.back() != '\n') {
      aggregate << '\n';
    }
    aggregate << "\n";
  }

  ethtool_capture.exit_code = aggregate_exit_code;
  ethtool_capture.command_available = true;
  ethtool_capture.output = aggregate.str();
  snapshot.raw_captures.push_back(std::move(ethtool_capture));
  SortInterfaces(snapshot.highlights);
}

} // namespace labops::hostprobe::internal

#endif
