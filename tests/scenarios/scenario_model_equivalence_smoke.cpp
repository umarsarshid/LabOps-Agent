#include "../common/assertions.hpp"
#include "scenarios/model.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using labops::tests::common::Fail;

template <typename T>
void AssertOptionalEqual(const std::optional<T>& lhs, const std::optional<T>& rhs,
                         std::string_view field_name) {
  if (lhs.has_value() != rhs.has_value()) {
    Fail("optional presence mismatch for field: " + std::string(field_name));
  }
  if (!lhs.has_value()) {
    return;
  }
  if (lhs.value() != rhs.value()) {
    Fail("optional value mismatch for field: " + std::string(field_name));
  }
}

void AssertModelEquivalent(const labops::scenarios::ScenarioModel& canonical,
                           const labops::scenarios::ScenarioModel& legacy) {
  AssertOptionalEqual(canonical.duration.duration_ms, legacy.duration.duration_ms,
                      "duration.duration_ms");
  AssertOptionalEqual(canonical.duration.duration_s, legacy.duration.duration_s,
                      "duration.duration_s");

  AssertOptionalEqual(canonical.backend, legacy.backend, "backend");
  AssertOptionalEqual(canonical.apply_mode, legacy.apply_mode, "apply_mode");

  AssertOptionalEqual(canonical.camera.fps, legacy.camera.fps, "camera.fps");
  AssertOptionalEqual(canonical.camera.frame_size_bytes, legacy.camera.frame_size_bytes,
                      "camera.frame_size_bytes");
  AssertOptionalEqual(canonical.camera.pixel_format, legacy.camera.pixel_format,
                      "camera.pixel_format");
  AssertOptionalEqual(canonical.camera.exposure_us, legacy.camera.exposure_us,
                      "camera.exposure_us");
  AssertOptionalEqual(canonical.camera.gain_db, legacy.camera.gain_db, "camera.gain_db");
  AssertOptionalEqual(canonical.camera.trigger_mode, legacy.camera.trigger_mode,
                      "camera.trigger_mode");
  AssertOptionalEqual(canonical.camera.trigger_source, legacy.camera.trigger_source,
                      "camera.trigger_source");
  AssertOptionalEqual(canonical.camera.trigger_activation, legacy.camera.trigger_activation,
                      "camera.trigger_activation");
  AssertOptionalEqual(canonical.camera.packet_size_bytes, legacy.camera.packet_size_bytes,
                      "camera.network.packet_size_bytes");
  AssertOptionalEqual(canonical.camera.inter_packet_delay_us, legacy.camera.inter_packet_delay_us,
                      "camera.network.inter_packet_delay_us");
  AssertOptionalEqual(canonical.camera.roi, legacy.camera.roi, "camera.roi");

  AssertOptionalEqual(canonical.sim_faults.seed, legacy.sim_faults.seed, "sim_faults.seed");
  AssertOptionalEqual(canonical.sim_faults.jitter_us, legacy.sim_faults.jitter_us,
                      "sim_faults.jitter_us");
  AssertOptionalEqual(canonical.sim_faults.drop_every_n, legacy.sim_faults.drop_every_n,
                      "sim_faults.drop_every_n");
  AssertOptionalEqual(canonical.sim_faults.drop_percent, legacy.sim_faults.drop_percent,
                      "sim_faults.drop_percent");
  AssertOptionalEqual(canonical.sim_faults.burst_drop, legacy.sim_faults.burst_drop,
                      "sim_faults.burst_drop");
  AssertOptionalEqual(canonical.sim_faults.reorder, legacy.sim_faults.reorder,
                      "sim_faults.reorder");

  AssertOptionalEqual(canonical.thresholds.min_avg_fps, legacy.thresholds.min_avg_fps,
                      "thresholds.min_avg_fps");
  AssertOptionalEqual(canonical.thresholds.max_drop_rate_percent,
                      legacy.thresholds.max_drop_rate_percent, "thresholds.max_drop_rate_percent");
  AssertOptionalEqual(canonical.thresholds.max_inter_frame_interval_p95_us,
                      legacy.thresholds.max_inter_frame_interval_p95_us,
                      "thresholds.max_inter_frame_interval_p95_us");
  AssertOptionalEqual(canonical.thresholds.max_inter_frame_jitter_p95_us,
                      legacy.thresholds.max_inter_frame_jitter_p95_us,
                      "thresholds.max_inter_frame_jitter_p95_us");
  AssertOptionalEqual(canonical.thresholds.max_disconnect_count,
                      legacy.thresholds.max_disconnect_count, "thresholds.max_disconnect_count");

  AssertOptionalEqual(canonical.netem_profile, legacy.netem_profile, "netem_profile");
  AssertOptionalEqual(canonical.device_selector, legacy.device_selector, "device_selector");
}

} // namespace

int main() {
  {
    const std::string canonical_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "model_equivalence",
  "backend": "real_stub",
  "apply_mode": "best_effort",
  "duration": { "duration_ms": 4500 },
  "camera": {
    "fps": 45,
    "frame_size_bytes": 8192,
    "pixel_format": "mono8",
    "exposure_us": 1200,
    "gain_db": 2.5,
    "trigger_mode": "hardware",
    "trigger_source": "line1",
    "trigger_activation": "rising_edge",
    "roi": { "x": 16, "y": 32, "width": 640, "height": 480 },
    "network": {
      "packet_size_bytes": 9000,
      "inter_packet_delay_us": 200
    }
  },
  "sim_faults": {
    "seed": 99,
    "jitter_us": 40,
    "drop_every_n": 3,
    "drop_percent": 7,
    "burst_drop": 2,
    "reorder": 1
  },
  "thresholds": {
    "min_avg_fps": 30,
    "max_drop_rate_percent": 10,
    "max_inter_frame_interval_p95_us": 50000,
    "max_inter_frame_jitter_p95_us": 5000,
    "max_disconnect_count": 2
  },
  "netem_profile": "jitter_light",
  "device_selector": "serial:SN-777,index:0"
}
)json";

    const std::string legacy_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "model_equivalence",
  "backend": "real_stub",
  "apply_mode": "best_effort",
  "duration_ms": 4500,
  "fps": 45,
  "frame_size_bytes": 8192,
  "pixel_format": "mono8",
  "exposure_us": 1200,
  "gain_db": 2.5,
  "trigger_mode": "hardware",
  "trigger_source": "line1",
  "trigger_activation": "rising_edge",
  "roi": { "x": 16, "y": 32, "width": 640, "height": 480 },
  "packet_size_bytes": 9000,
  "inter_packet_delay_us": 200,
  "seed": 99,
  "jitter_us": 40,
  "drop_every_n": 3,
  "drop_percent": 7,
  "burst_drop": 2,
  "reorder": 1,
  "min_avg_fps": 30,
  "max_drop_rate_percent": 10,
  "max_inter_frame_interval_p95_us": 50000,
  "max_inter_frame_jitter_p95_us": 5000,
  "max_disconnect_count": 2,
  "netem_profile": "jitter_light",
  "device_selector": "serial:SN-777,index:0"
}
)json";

    labops::scenarios::ScenarioModel canonical_model;
    labops::scenarios::ScenarioModel legacy_model;
    std::string error;

    if (!labops::scenarios::ParseScenarioModelText(canonical_json, canonical_model, error)) {
      Fail("failed to parse canonical scenario model: " + error);
    }
    if (!labops::scenarios::ParseScenarioModelText(legacy_json, legacy_model, error)) {
      Fail("failed to parse legacy scenario model: " + error);
    }

    AssertModelEquivalent(canonical_model, legacy_model);
  }

  {
    // Run-path parsing stays lenient by design: unexpected field types are
    // treated as unset so older fixtures can still execute while validator
    // remains the strict schema gate.
    const std::string lenient_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "lenient_types",
  "duration": { "duration_ms": "bad" },
  "camera": { "fps": "oops" },
  "sim_faults": { "drop_percent": "nan" },
  "thresholds": { "max_disconnect_count": "bad" }
}
)json";

    labops::scenarios::ScenarioModel model;
    std::string error;
    if (!labops::scenarios::ParseScenarioModelText(lenient_json, model, error)) {
      Fail("lenient parse should not fail on type mismatch: " + error);
    }

    if (model.duration.duration_ms.has_value() || model.camera.fps.has_value() ||
        model.sim_faults.drop_percent.has_value() ||
        model.thresholds.max_disconnect_count.has_value()) {
      Fail("type mismatch fields should be treated as unset");
    }
  }

  {
    const std::string bad_roi_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "bad_roi",
  "camera": { "roi": { "x": 0, "y": 0, "width": 320 } }
}
)json";

    labops::scenarios::ScenarioModel model;
    std::string error;
    if (labops::scenarios::ParseScenarioModelText(bad_roi_json, model, error)) {
      Fail("expected ROI parse error for missing height");
    }
    if (error.find("camera.roi") == std::string::npos) {
      Fail("expected ROI parse error message to mention camera.roi");
    }
  }

  std::cout << "scenario_model_equivalence_smoke: ok\n";
  return 0;
}
