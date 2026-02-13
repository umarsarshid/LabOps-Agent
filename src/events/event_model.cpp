#include "events/event_model.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace labops::events {

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
  // Millisecond precision keeps enough timing detail for triage while staying
  // compact and readable in JSONL logs.
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

std::string ToJson(EventType event_type) {
  switch (event_type) {
  case EventType::kRunStarted:
    return "run_started";
  case EventType::kStreamStarted:
    return "STREAM_STARTED";
  case EventType::kFrameReceived:
    return "FRAME_RECEIVED";
  case EventType::kFrameDropped:
    return "FRAME_DROPPED";
  case EventType::kStreamStopped:
    return "STREAM_STOPPED";
  case EventType::kInfo:
    return "info";
  case EventType::kWarning:
    return "warning";
  case EventType::kError:
    return "error";
  }

  return "unknown";
}

std::string ToJson(const Event& event) {
  std::ostringstream out;
  out << "{"
      << "\"ts_utc\":\"" << FormatUtcTimestamp(event.ts) << "\","
      << "\"type\":\"" << ToJson(event.type) << "\","
      << "\"payload\":{";

  // `payload` is a std::map, so key iteration order is stable. This keeps
  // line-by-line diffs and snapshot tests deterministic.
  bool first = true;
  for (const auto& [key, value] : event.payload) {
    if (!first) {
      out << ',';
    }
    out << "\"" << EscapeJson(key) << "\":\"" << EscapeJson(value) << "\"";
    first = false;
  }

  out << "}}";
  return out.str();
}

} // namespace labops::events
