#include "agent/playbook.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

void AssertKnobOrder(const std::vector<labops::agent::PlaybookKnob>& knobs,
                     const std::vector<std::string_view>& expected) {
  if (knobs.size() != expected.size()) {
    Fail("unexpected knob count");
  }

  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (knobs[i].name != expected[i]) {
      std::cerr << "knob order mismatch at index " << i << ": expected '" << expected[i]
                << "' got '" << knobs[i].name << "'\n";
      std::abort();
    }
  }
}

} // namespace

int main() {
  labops::agent::Playbook playbook;
  std::string error;

  if (!labops::agent::SelectPlaybookForSymptom("Dropped Frames", playbook, error)) {
    Fail("expected dropped-frames symptom to resolve to a playbook");
  }
  if (!error.empty()) {
    Fail("error should be empty on successful playbook selection");
  }

  if (playbook.id != "dropped_frames_oaat_v1") {
    Fail("unexpected playbook id");
  }
  if (playbook.symptom != "dropped_frames") {
    Fail("unexpected normalized symptom id");
  }

  AssertKnobOrder(playbook.knobs,
                  {
                      "packet_delay_ms",
                      "fps",
                      "roi_enabled",
                      "reorder_percent",
                      "loss_percent",
                  });

  if (labops::agent::SelectPlaybookForSymptom("disconnects", playbook, error)) {
    Fail("unknown symptom should not resolve to a playbook");
  }
  AssertContains(error, "no playbook registered for symptom");
  AssertContains(error, "available symptoms: dropped_frames");

  std::cout << "playbook_selection_smoke: ok\n";
  return 0;
}
