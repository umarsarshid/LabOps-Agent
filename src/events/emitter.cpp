#include "events/emitter.hpp"

#include "events/jsonl_writer.hpp"

#include <utility>

namespace labops::events {

namespace {

EventType ToEventType(const Emitter::FrameOutcomeKind outcome) {
  switch (outcome) {
  case Emitter::FrameOutcomeKind::kDropped:
    return EventType::kFrameDropped;
  case Emitter::FrameOutcomeKind::kTimeout:
    return EventType::kFrameTimeout;
  case Emitter::FrameOutcomeKind::kIncomplete:
    return EventType::kFrameIncomplete;
  case Emitter::FrameOutcomeKind::kReceived:
  default:
    return EventType::kFrameReceived;
  }
}

} // namespace

Emitter::Emitter(const std::filesystem::path& output_dir, std::filesystem::path& events_path)
    : output_dir_(output_dir), events_path_(events_path) {}

bool Emitter::EmitRaw(EventType type, std::chrono::system_clock::time_point ts,
                      std::map<std::string, std::string> payload, std::string& error) const {
  Event event;
  event.ts = ts;
  event.type = type;
  event.payload = std::move(payload);
  return AppendEventJsonl(event, output_dir_, events_path_, error);
}

bool Emitter::EmitStreamStarted(const StreamStartedEvent& event, std::string& error) const {
  return EmitRaw(EventType::kStreamStarted, event.ts,
                 {
                     {"run_id", event.run_id},
                     {"scenario_id", event.scenario_id},
                     {"backend", event.backend},
                     {"duration_ms", std::to_string(event.duration_ms)},
                     {"fps", std::to_string(event.fps)},
                     {"seed", std::to_string(event.seed)},
                     {"soak_mode", event.soak_mode ? "true" : "false"},
                     {"resume", event.resume ? "true" : "false"},
                 },
                 error);
}

bool Emitter::EmitFrameOutcome(const FrameOutcomeEvent& event, std::string& error) const {
  std::map<std::string, std::string> payload = {
      {"run_id", event.run_id},
      {"frame_id", std::to_string(event.frame_id)},
      {"size_bytes", std::to_string(event.size_bytes)},
      {"dropped", event.dropped ? "true" : "false"},
  };
  if (event.dropped) {
    payload["reason"] = event.reason.has_value() ? event.reason.value() : "backend_marked_dropped";
  }
  return EmitRaw(ToEventType(event.outcome), event.ts, std::move(payload), error);
}

bool Emitter::EmitConfigStatus(const ConfigStatusEvent& event, std::string& error) const {
  switch (event.kind) {
  case ConfigStatusEvent::Kind::kApplied: {
    std::map<std::string, std::string> payload = {
        {"run_id", event.run_id},
        {"scenario_id", event.scenario_id},
        {"applied_count", std::to_string(event.applied_params.size())},
    };

    // Prefix backend params so run-level metadata fields remain unambiguous.
    for (const auto& [key, value] : event.applied_params) {
      payload["param." + key] = value;
    }
    return EmitRaw(EventType::kConfigApplied, event.ts, std::move(payload), error);
  }
  case ConfigStatusEvent::Kind::kUnsupported:
    return EmitRaw(EventType::kConfigUnsupported, event.ts,
                   {
                       {"run_id", event.run_id},
                       {"scenario_id", event.scenario_id},
                       {"apply_mode", event.apply_mode},
                       {"generic_key", event.generic_key},
                       {"requested_value", event.requested_value},
                       {"reason", event.reason},
                   },
                   error);
  case ConfigStatusEvent::Kind::kAdjusted:
    return EmitRaw(EventType::kConfigAdjusted, event.ts,
                   {
                       {"run_id", event.run_id},
                       {"scenario_id", event.scenario_id},
                       {"apply_mode", event.apply_mode},
                       {"generic_key", event.generic_key},
                       {"node_name", event.node_name},
                       {"requested_value", event.requested_value},
                       {"applied_value", event.applied_value},
                       {"reason", event.reason},
                   },
                   error);
  }

  error = "unsupported config status event kind";
  return false;
}

bool Emitter::EmitConfigApplied(const ConfigAppliedEvent& event, std::string& error) const {
  return EmitConfigStatus(
      {
          .kind = ConfigStatusEvent::Kind::kApplied,
          .ts = event.ts,
          .run_id = event.run_id,
          .scenario_id = event.scenario_id,
          .applied_params = event.applied_params,
      },
      error);
}

bool Emitter::EmitConfigUnsupported(const ConfigUnsupportedEvent& event, std::string& error) const {
  return EmitConfigStatus(
      {
          .kind = ConfigStatusEvent::Kind::kUnsupported,
          .ts = event.ts,
          .run_id = event.run_id,
          .scenario_id = event.scenario_id,
          .apply_mode = event.apply_mode,
          .generic_key = event.generic_key,
          .requested_value = event.requested_value,
          .reason = event.reason,
      },
      error);
}

bool Emitter::EmitConfigAdjusted(const ConfigAdjustedEvent& event, std::string& error) const {
  return EmitConfigStatus(
      {
          .kind = ConfigStatusEvent::Kind::kAdjusted,
          .ts = event.ts,
          .run_id = event.run_id,
          .scenario_id = event.scenario_id,
          .apply_mode = event.apply_mode,
          .generic_key = event.generic_key,
          .requested_value = event.requested_value,
          .reason = event.reason,
          .node_name = event.node_name,
          .applied_value = event.applied_value,
      },
      error);
}

bool Emitter::EmitTransportAnomaly(const TransportAnomalyEvent& event, std::string& error) const {
  return EmitRaw(EventType::kTransportAnomaly, event.ts,
                 {
                     {"run_id", event.run_id},
                     {"scenario_id", event.scenario_id},
                     {"heuristic_id", event.heuristic_id},
                     {"counter", event.counter},
                     {"observed_value", std::to_string(event.observed_value)},
                     {"threshold", std::to_string(event.threshold)},
                     {"summary", event.summary},
                 },
                 error);
}

} // namespace labops::events
