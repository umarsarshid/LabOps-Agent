#ifndef LABOPS_CORE_TIME_UTILS_HPP_
#define LABOPS_CORE_TIME_UTILS_HPP_

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace labops::core {

// Canonical UTC timestamp formatter used by contracts and event streams.
// Millisecond precision keeps traces readable while preserving triage value.
inline std::string FormatUtcTimestamp(std::chrono::system_clock::time_point timestamp) {
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
  out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis_component << 'Z';
  return out.str();
}

} // namespace labops::core

#endif // LABOPS_CORE_TIME_UTILS_HPP_
