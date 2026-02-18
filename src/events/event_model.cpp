#include "events/event_model.hpp"
#include "core/json_utils.hpp"
#include "core/time_utils.hpp"

#include <sstream>

namespace labops::events {

std::string ToJson(EventType event_type) {
  switch (event_type) {
  case EventType::kRunStarted:
    return "run_started";
  case EventType::kConfigApplied:
    return "CONFIG_APPLIED";
  case EventType::kConfigUnsupported:
    return "CONFIG_UNSUPPORTED";
  case EventType::kConfigAdjusted:
    return "CONFIG_ADJUSTED";
  case EventType::kStreamStarted:
    return "STREAM_STARTED";
  case EventType::kFrameReceived:
    return "FRAME_RECEIVED";
  case EventType::kFrameDropped:
    return "FRAME_DROPPED";
  case EventType::kFrameTimeout:
    return "FRAME_TIMEOUT";
  case EventType::kFrameIncomplete:
    return "FRAME_INCOMPLETE";
  case EventType::kDeviceDisconnected:
    return "DEVICE_DISCONNECTED";
  case EventType::kTransportAnomaly:
    return "TRANSPORT_ANOMALY";
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
      << "\"ts_utc\":\"" << core::FormatUtcTimestamp(event.ts) << "\","
      << "\"type\":\"" << ToJson(event.type) << "\","
      << "\"payload\":{";

  // `payload` is a std::map, so key iteration order is stable. This keeps
  // line-by-line diffs and snapshot tests deterministic.
  bool first = true;
  for (const auto& [key, value] : event.payload) {
    if (!first) {
      out << ',';
    }
    out << "\"" << core::EscapeJson(key) << "\":\"" << core::EscapeJson(value) << "\"";
    first = false;
  }

  out << "}}";
  return out.str();
}

} // namespace labops::events
