#pragma once

#include <chrono>
#include <map>
#include <string>

namespace labops::events {

// Normalized event categories emitted by runners/backends. Keep this enum
// compact and stable because downstream metrics and triage logic key off it.
enum class EventType {
  kRunStarted,
  kConfigApplied,
  kConfigUnsupported,
  kConfigAdjusted,
  kStreamStarted,
  kFrameReceived,
  kFrameDropped,
  kStreamStopped,
  kInfo,
  kWarning,
  kError,
};

// Canonical timeline event contract.
//
// - `ts`: UTC timestamp when the event occurred.
// - `type`: normalized category.
// - `payload`: lightweight string key/value attributes for context.
struct Event {
  std::chrono::system_clock::time_point ts{};
  EventType type = EventType::kInfo;
  std::map<std::string, std::string> payload;
};

// JSON serializers used by JSONL writers and tests.
std::string ToJson(EventType event_type);
std::string ToJson(const Event& event);

} // namespace labops::events
