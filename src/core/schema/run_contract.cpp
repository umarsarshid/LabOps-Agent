#include "core/schema/run_contract.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace labops::core::schema {

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
  out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis_component << 'Z';
  return out.str();
}

} // namespace

std::string ToJson(const RunConfig& run_config) {
  std::ostringstream out;
  out << "{"
      << "\"scenario_id\":\"" << EscapeJson(run_config.scenario_id) << "\","
      << "\"backend\":\"" << EscapeJson(run_config.backend) << "\","
      << "\"seed\":" << run_config.seed << ","
      << "\"duration_ms\":" << run_config.duration.count() << "}";
  return out.str();
}

std::string ToJson(const RealDeviceMetadata& real_device) {
  std::ostringstream out;
  out << "{"
      << "\"model\":\"" << EscapeJson(real_device.model) << "\","
      << "\"serial\":\"" << EscapeJson(real_device.serial) << "\","
      << "\"transport\":\"" << EscapeJson(real_device.transport) << "\"";
  if (real_device.user_id.has_value()) {
    out << ",\"user_id\":\"" << EscapeJson(real_device.user_id.value()) << "\"";
  }
  if (real_device.firmware_version.has_value()) {
    out << ",\"firmware_version\":\"" << EscapeJson(real_device.firmware_version.value()) << "\"";
  }
  if (real_device.sdk_version.has_value()) {
    out << ",\"sdk_version\":\"" << EscapeJson(real_device.sdk_version.value()) << "\"";
  }
  out << "}";
  return out.str();
}

std::string ToJson(const RunInfo& run_info) {
  std::ostringstream out;
  out << "{"
      << "\"run_id\":\"" << EscapeJson(run_info.run_id) << "\","
      << "\"config\":" << ToJson(run_info.config);
  if (run_info.real_device.has_value()) {
    out << ",\"real_device\":" << ToJson(run_info.real_device.value());
  }
  out << ","
      << "\"timestamps\":{"
      << "\"created_at_utc\":\"" << FormatUtcTimestamp(run_info.timestamps.created_at) << "\","
      << "\"started_at_utc\":\"" << FormatUtcTimestamp(run_info.timestamps.started_at) << "\","
      << "\"finished_at_utc\":\"" << FormatUtcTimestamp(run_info.timestamps.finished_at) << "\""
      << "}"
      << "}";
  return out.str();
}

} // namespace labops::core::schema
