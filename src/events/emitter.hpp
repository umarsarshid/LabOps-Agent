#pragma once

#include "events/event_model.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace labops::events {

// Thin event facade used by run orchestration to keep payload contracts
// consistent while still writing the same JSONL event format.
class Emitter {
public:
  enum class FrameOutcomeKind {
    kReceived,
    kDropped,
    kTimeout,
    kIncomplete,
  };

  struct StreamStartedEvent {
    std::chrono::system_clock::time_point ts{};
    std::string run_id;
    std::string scenario_id;
    std::string backend;
    std::uint64_t duration_ms = 0;
    std::uint32_t fps = 0;
    std::uint64_t seed = 0;
    bool soak_mode = false;
    bool resume = false;
  };

  struct FrameOutcomeEvent {
    std::chrono::system_clock::time_point ts{};
    FrameOutcomeKind outcome = FrameOutcomeKind::kReceived;
    std::string run_id;
    std::uint64_t frame_id = 0;
    std::uint64_t size_bytes = 0;
    bool dropped = false;
    std::optional<std::string> reason;
  };

  struct ConfigAppliedEvent {
    std::chrono::system_clock::time_point ts{};
    std::string run_id;
    std::string scenario_id;
    std::map<std::string, std::string> applied_params;
  };

  struct ConfigUnsupportedEvent {
    std::chrono::system_clock::time_point ts{};
    std::string run_id;
    std::string scenario_id;
    std::string apply_mode;
    std::string generic_key;
    std::string requested_value;
    std::string reason;
  };

  struct ConfigAdjustedEvent {
    std::chrono::system_clock::time_point ts{};
    std::string run_id;
    std::string scenario_id;
    std::string apply_mode;
    std::string generic_key;
    std::string node_name;
    std::string requested_value;
    std::string applied_value;
    std::string reason;
  };

  // Unified config-status event input used by run orchestration when emitting
  // apply diagnostics. This keeps payload contracts centralized for:
  // - CONFIG_APPLIED
  // - CONFIG_UNSUPPORTED
  // - CONFIG_ADJUSTED
  struct ConfigStatusEvent {
    enum class Kind {
      kApplied,
      kUnsupported,
      kAdjusted,
    };

    Kind kind = Kind::kApplied;
    std::chrono::system_clock::time_point ts{};
    std::string run_id;
    std::string scenario_id;

    // kApplied fields.
    std::map<std::string, std::string> applied_params;

    // kUnsupported / kAdjusted shared fields.
    std::string apply_mode;
    std::string generic_key;
    std::string requested_value;
    std::string reason;

    // kAdjusted-specific fields.
    std::string node_name;
    std::string applied_value;
  };

  struct TransportAnomalyEvent {
    std::chrono::system_clock::time_point ts{};
    std::string run_id;
    std::string scenario_id;
    std::string heuristic_id;
    std::string counter;
    std::uint64_t observed_value = 0;
    std::uint64_t threshold = 0;
    std::string summary;
  };

  Emitter(const std::filesystem::path& output_dir, std::filesystem::path& events_path);

  bool EmitRaw(EventType type, std::chrono::system_clock::time_point ts,
               std::map<std::string, std::string> payload, std::string& error) const;

  bool EmitStreamStarted(const StreamStartedEvent& event, std::string& error) const;
  bool EmitFrameOutcome(const FrameOutcomeEvent& event, std::string& error) const;
  bool EmitConfigStatus(const ConfigStatusEvent& event, std::string& error) const;
  bool EmitConfigApplied(const ConfigAppliedEvent& event, std::string& error) const;
  bool EmitConfigUnsupported(const ConfigUnsupportedEvent& event, std::string& error) const;
  bool EmitConfigAdjusted(const ConfigAdjustedEvent& event, std::string& error) const;
  bool EmitTransportAnomaly(const TransportAnomalyEvent& event, std::string& error) const;

private:
  const std::filesystem::path& output_dir_;
  std::filesystem::path& events_path_;
};

} // namespace labops::events
