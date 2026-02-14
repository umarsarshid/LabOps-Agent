#include "agent/playbook.hpp"

#include <algorithm>
#include <cctype>

namespace labops::agent {

namespace {

std::string NormalizeSymptom(std::string_view raw) {
  std::string normalized;
  normalized.reserve(raw.size());

  bool previous_was_separator = false;
  for (const char c : raw) {
    const unsigned char as_unsigned = static_cast<unsigned char>(c);

    if (std::isalnum(as_unsigned) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(as_unsigned)));
      previous_was_separator = false;
      continue;
    }

    const bool is_separator = c == ' ' || c == '_' || c == '-';
    if (!is_separator) {
      continue;
    }

    if (!normalized.empty() && !previous_was_separator) {
      normalized.push_back('_');
      previous_was_separator = true;
    }
  }

  while (!normalized.empty() && normalized.front() == '_') {
    normalized.erase(normalized.begin());
  }
  while (!normalized.empty() && normalized.back() == '_') {
    normalized.pop_back();
  }

  return normalized;
}

Playbook BuildDroppedFramesPlaybook() {
  Playbook playbook;
  playbook.id = "dropped_frames_oaat_v1";
  playbook.symptom = "dropped_frames";
  playbook.objective =
      "Isolate which single streaming/network knob causes frame loss relative to baseline.";

  // Order is deliberate: start with network-latency stress, then camera-side
  // rate/ROI pressure, then explicit transport disorder/loss knobs.
  playbook.knobs = {
      {"packet_delay_ms", "Test sensitivity to transport latency spikes."},
      {"fps", "Lower/raise frame cadence to separate throughput vs latency issues."},
      {"roi_enabled", "Check whether ROI path changes bandwidth or processing load."},
      {"reorder_percent", "Test sensitivity to out-of-order packet delivery."},
      {"loss_percent", "Measure tolerance to packet loss under controlled impairment."},
  };

  return playbook;
}

} // namespace

bool SelectPlaybookForSymptom(std::string_view symptom_input, Playbook& playbook,
                              std::string& error) {
  playbook = Playbook{};
  error.clear();

  const std::string normalized = NormalizeSymptom(symptom_input);
  if (normalized.empty()) {
    error = "symptom input cannot be empty";
    return false;
  }

  if (normalized == "dropped_frames" || normalized == "frame_drops" || normalized == "drops") {
    playbook = BuildDroppedFramesPlaybook();
    return true;
  }

  error = "no playbook registered for symptom '" + std::string(symptom_input) + "' (normalized='" +
          normalized + "'). available symptoms: dropped_frames";
  return false;
}

} // namespace labops::agent
