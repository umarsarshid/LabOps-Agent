#include "hostprobe/system_probe.hpp"

#include <array>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <thread>
#include <time.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace labops::hostprobe {

namespace {

std::string EscapeJson(std::string_view input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default: {
      const auto as_unsigned = static_cast<unsigned char>(ch);
      if (as_unsigned < 0x20U) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(as_unsigned) << std::dec << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
    }
  }
  return out.str();
}

std::string FormatUtcTimestamp(std::chrono::system_clock::time_point timestamp) {
  const auto millis_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
  const auto millis_component = static_cast<int>((millis_since_epoch % 1000 + 1000) % 1000);

  const std::time_t epoch_seconds = std::chrono::system_clock::to_time_t(timestamp);
  std::tm utc_time{};
#if defined(_WIN32)
  const errno_t result = gmtime_s(&utc_time, &epoch_seconds);
  if (result != 0) {
    return "";
  }
#else
  const std::tm* result = gmtime_r(&epoch_seconds, &utc_time);
  if (result == nullptr) {
    return "";
  }
#endif

  std::ostringstream out;
  out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
      << std::setfill('0') << millis_component << 'Z';
  return out.str();
}

std::string FormatDouble(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::string DetectOsName() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string DetectOsVersion() {
#if defined(__linux__) || defined(__APPLE__)
  struct utsname uts {};
  if (uname(&uts) == 0) {
    return std::string(uts.release);
  }
#endif
  return "unknown";
}

std::string ProbeCpuModel() {
#if defined(__linux__)
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
#elif defined(__APPLE__)
  char buffer[256] = {};
  std::size_t length = sizeof(buffer);
  if (sysctlbyname("machdep.cpu.brand_string", buffer, &length, nullptr, 0) == 0 &&
      length > 0U) {
    return std::string(buffer);
  }
#endif
  return "unknown";
}

std::uint64_t ProbeRamTotalBytes() {
#if defined(__linux__)
  struct sysinfo info {};
  if (sysinfo(&info) == 0) {
    return static_cast<std::uint64_t>(info.totalram) *
           static_cast<std::uint64_t>(info.mem_unit);
  }
#elif defined(__APPLE__)
  std::uint64_t value = 0;
  std::size_t length = sizeof(value);
  if (sysctlbyname("hw.memsize", &value, &length, nullptr, 0) == 0) {
    return value;
  }
#elif defined(_WIN32)
  MEMORYSTATUSEX memory_status {};
  memory_status.dwLength = sizeof(memory_status);
  if (GlobalMemoryStatusEx(&memory_status) != 0) {
    return static_cast<std::uint64_t>(memory_status.ullTotalPhys);
  }
#endif
  return 0;
}

std::uint64_t ProbeUptimeSeconds() {
#if defined(__linux__)
  struct sysinfo info {};
  if (sysinfo(&info) == 0 && info.uptime >= 0) {
    return static_cast<std::uint64_t>(info.uptime);
  }
#elif defined(__APPLE__)
  // Boot time via sysctl keeps this independent of sleep/wake counters.
  struct timeval boot_time {};
  std::size_t length = sizeof(boot_time);
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};
  if (sysctl(mib, 2, &boot_time, &length, nullptr, 0) == 0 && boot_time.tv_sec > 0) {
    const auto now_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (now_seconds > static_cast<std::int64_t>(boot_time.tv_sec)) {
      return static_cast<std::uint64_t>(now_seconds - static_cast<std::int64_t>(boot_time.tv_sec));
    }
  }

  // Fallback for environments where kern.boottime is unavailable.
  struct timespec uptime_spec {};
  if (clock_gettime(CLOCK_UPTIME_RAW, &uptime_spec) == 0 && uptime_spec.tv_sec >= 0) {
    return static_cast<std::uint64_t>(uptime_spec.tv_sec);
  }
#elif defined(_WIN32)
  return static_cast<std::uint64_t>(GetTickCount64() / 1000ULL);
#endif
  return 0;
}

std::array<std::optional<double>, 3> ProbeLoadAverages() {
  std::array<std::optional<double>, 3> values;
#if defined(__linux__) || defined(__APPLE__)
  double loads[3] = {0.0, 0.0, 0.0};
  if (getloadavg(loads, 3) == 3) {
    values[0] = loads[0];
    values[1] = loads[1];
    values[2] = loads[2];
  }
#endif
  return values;
}

} // namespace

bool CollectHostProbeSnapshot(HostProbeSnapshot& snapshot, std::string& error) {
  error.clear();
  snapshot = HostProbeSnapshot{};

  snapshot.captured_at = std::chrono::system_clock::now();
  snapshot.os_name = DetectOsName();
  snapshot.os_version = DetectOsVersion();
  snapshot.cpu_model = ProbeCpuModel();
  snapshot.cpu_logical_cores = std::thread::hardware_concurrency();
  snapshot.ram_total_bytes = ProbeRamTotalBytes();
  snapshot.uptime_seconds = ProbeUptimeSeconds();

  const auto load_averages = ProbeLoadAverages();
  snapshot.load_avg_1m = load_averages[0];
  snapshot.load_avg_5m = load_averages[1];
  snapshot.load_avg_15m = load_averages[2];
  return true;
}

std::string ToJson(const HostProbeSnapshot& snapshot) {
  std::ostringstream out;
  out << "{"
      << "\"captured_at_utc\":\"" << FormatUtcTimestamp(snapshot.captured_at) << "\","
      << "\"os\":{"
      << "\"name\":\"" << EscapeJson(snapshot.os_name) << "\","
      << "\"version\":\"" << EscapeJson(snapshot.os_version) << "\""
      << "},"
      << "\"cpu\":{"
      << "\"model\":\"" << EscapeJson(snapshot.cpu_model) << "\","
      << "\"logical_cores\":" << snapshot.cpu_logical_cores
      << "},"
      << "\"ram_total_bytes\":" << snapshot.ram_total_bytes << ","
      << "\"uptime_seconds\":" << snapshot.uptime_seconds << ","
      << "\"load_avg\":{"
      << "\"one_min\":";
  if (snapshot.load_avg_1m.has_value()) {
    out << FormatDouble(snapshot.load_avg_1m.value());
  } else {
    out << "null";
  }
  out << ",\"five_min\":";
  if (snapshot.load_avg_5m.has_value()) {
    out << FormatDouble(snapshot.load_avg_5m.value());
  } else {
    out << "null";
  }
  out << ",\"fifteen_min\":";
  if (snapshot.load_avg_15m.has_value()) {
    out << FormatDouble(snapshot.load_avg_15m.value());
  } else {
    out << "null";
  }
  out << "}"
      << "}";
  return out.str();
}

} // namespace labops::hostprobe
