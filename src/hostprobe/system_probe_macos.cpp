#include "hostprobe/system_probe_internal.hpp"

#if defined(__APPLE__)

#include <chrono>
#include <sstream>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
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
  char buffer[256] = {};
  std::size_t length = sizeof(buffer);
  if (sysctlbyname("machdep.cpu.brand_string", buffer, &length, nullptr, 0) == 0 && length > 0U) {
    return std::string(buffer);
  }
  return "unknown";
}

std::uint64_t ProbeRamTotalBytesPlatform() {
  std::uint64_t value = 0;
  std::size_t length = sizeof(value);
  if (sysctlbyname("hw.memsize", &value, &length, nullptr, 0) == 0) {
    return value;
  }
  return 0;
}

std::uint64_t ProbeUptimeSecondsPlatform() {
  // Boot time via sysctl keeps this independent of sleep/wake counters.
  struct timeval boot_time{};
  std::size_t length = sizeof(boot_time);
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};
  if (sysctl(mib, 2, &boot_time, &length, nullptr, 0) == 0 && boot_time.tv_sec > 0) {
    const auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    if (now_seconds > static_cast<std::int64_t>(boot_time.tv_sec)) {
      return static_cast<std::uint64_t>(now_seconds - static_cast<std::int64_t>(boot_time.tv_sec));
    }
  }

  // Fallback for environments where kern.boottime is unavailable.
  struct timespec uptime_spec{};
  if (clock_gettime(CLOCK_UPTIME_RAW, &uptime_spec) == 0 && uptime_spec.tv_sec >= 0) {
    return static_cast<std::uint64_t>(uptime_spec.tv_sec);
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
  NicCommandCapture ifconfig_a = CaptureCommand("nic_ifconfig_a.txt", "ifconfig -a");
  ParseMacIfconfigOutput(ifconfig_a.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ifconfig_a));

  NicCommandCapture netstat_rn = CaptureCommand("nic_netstat_rn.txt", "netstat -rn");
  ParseMacNetstatRouteOutput(netstat_rn.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(netstat_rn));

  NicCommandCapture route_default =
      CaptureCommand("nic_route_get_default.txt", "route -n get default");
  ParseMacRouteGetDefaultOutput(route_default.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(route_default));

  SortInterfaces(snapshot.highlights);
}

} // namespace labops::hostprobe::internal

#endif
