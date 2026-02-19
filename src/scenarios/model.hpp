#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace labops::scenarios {

// Parsed scenario values used by runtime planning.
//
// Design notes:
// - This model intentionally keeps fields optional so run loading can preserve
//   historical lenient behavior (unexpected types are treated as "unset").
// - Validator remains the strict schema gate; this model is the single parser
//   for run planning inputs, including legacy key fallbacks.
struct ScenarioModel {
  struct Duration {
    std::optional<std::uint64_t> duration_ms;
    std::optional<std::uint64_t> duration_s;
  } duration;

  struct Roi {
    std::uint64_t x = 0;
    std::uint64_t y = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;

    bool operator==(const Roi& other) const = default;
  };

  struct Camera {
    std::optional<std::uint64_t> fps;
    std::optional<std::uint64_t> frame_size_bytes;
    std::optional<std::string> pixel_format;
    std::optional<std::uint64_t> exposure_us;
    std::optional<double> gain_db;
    std::optional<std::string> trigger_mode;
    std::optional<std::string> trigger_source;
    std::optional<std::string> trigger_activation;
    std::optional<std::uint64_t> packet_size_bytes;
    std::optional<std::uint64_t> inter_packet_delay_us;
    std::optional<Roi> roi;
  } camera;

  struct SimFaults {
    std::optional<std::uint64_t> seed;
    std::optional<std::uint64_t> jitter_us;
    std::optional<std::uint64_t> drop_every_n;
    std::optional<std::uint64_t> drop_percent;
    std::optional<std::uint64_t> burst_drop;
    std::optional<std::uint64_t> reorder;
  } sim_faults;

  struct Thresholds {
    std::optional<double> min_avg_fps;
    std::optional<double> max_drop_rate_percent;
    std::optional<double> max_inter_frame_interval_p95_us;
    std::optional<double> max_inter_frame_jitter_p95_us;
    // Keep as double so run loader can preserve existing integer-only checks.
    std::optional<double> max_disconnect_count;
  } thresholds;

  struct Webcam {
    struct DeviceSelector {
      std::optional<std::uint64_t> index;
      std::optional<std::string> id;
      std::optional<std::string> name_contains;

      bool operator==(const DeviceSelector& other) const = default;
    };

    std::optional<DeviceSelector> device_selector;
    std::optional<std::uint64_t> requested_width;
    std::optional<std::uint64_t> requested_height;
    std::optional<double> requested_fps;
    std::optional<std::string> requested_pixel_format;
  } webcam;

  std::optional<std::string> backend;
  std::optional<std::string> apply_mode;
  std::optional<std::string> netem_profile;
  std::optional<std::string> device_selector;
};

// Parses scenario JSON text into a runtime model used by run planning.
// Returns false on hard parse errors (invalid JSON/root type or invalid ROI object).
bool ParseScenarioModelText(std::string_view json_text, ScenarioModel& model, std::string& error);

// Loads and parses a scenario file into ScenarioModel.
bool LoadScenarioModelFile(const std::string& scenario_path, ScenarioModel& model,
                           std::string& error);

} // namespace labops::scenarios
