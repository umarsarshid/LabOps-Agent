#include "scenarios/model.hpp"

#include "core/json_dom.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace fs = std::filesystem;

namespace labops::scenarios {

namespace {

using JsonValue = core::json::Value;
using JsonParser = core::json::Parser;

const JsonValue* FindObjectMember(const JsonValue& object_value, std::string_view key) {
  if (object_value.type != JsonValue::Type::kObject) {
    return nullptr;
  }
  const auto it = object_value.object_value.find(std::string(key));
  if (it == object_value.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

const JsonValue* FindJsonPath(const JsonValue& root, std::initializer_list<std::string_view> path) {
  const JsonValue* cursor = &root;
  for (const std::string_view key : path) {
    cursor = FindObjectMember(*cursor, key);
    if (cursor == nullptr) {
      return nullptr;
    }
  }
  return cursor;
}

// Runtime parsing keeps support for both canonical schema paths and historical
// flat fixture keys so old smoke tests and scripts still execute.
const JsonValue* FindScenarioField(const JsonValue& root,
                                   std::initializer_list<std::string_view> canonical_path,
                                   std::initializer_list<std::string_view> legacy_path = {}) {
  if (const JsonValue* value = FindJsonPath(root, canonical_path); value != nullptr) {
    return value;
  }
  if (legacy_path.size() == 0U) {
    return nullptr;
  }
  return FindJsonPath(root, legacy_path);
}

bool TryGetNonNegativeInteger(const JsonValue& value, std::uint64_t& out) {
  if (value.type != JsonValue::Type::kNumber) {
    return false;
  }
  if (!std::isfinite(value.number_value) || value.number_value < 0.0) {
    return false;
  }
  const double floored = std::floor(value.number_value);
  if (floored != value.number_value ||
      floored > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return false;
  }
  out = static_cast<std::uint64_t>(floored);
  return true;
}

bool TryGetFiniteNumber(const JsonValue& value, double& out) {
  if (value.type != JsonValue::Type::kNumber || !std::isfinite(value.number_value)) {
    return false;
  }
  out = value.number_value;
  return true;
}

std::optional<std::uint64_t> ReadU64Field(const JsonValue& root,
                                          std::initializer_list<std::string_view> canonical_path,
                                          std::initializer_list<std::string_view> legacy_path) {
  const JsonValue* value = FindScenarioField(root, canonical_path, legacy_path);
  if (value == nullptr) {
    return std::nullopt;
  }
  std::uint64_t parsed = 0;
  if (!TryGetNonNegativeInteger(*value, parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<double> ReadNumberField(const JsonValue& root,
                                      std::initializer_list<std::string_view> canonical_path,
                                      std::initializer_list<std::string_view> legacy_path) {
  const JsonValue* value = FindScenarioField(root, canonical_path, legacy_path);
  if (value == nullptr) {
    return std::nullopt;
  }
  double parsed = 0.0;
  if (!TryGetFiniteNumber(*value, parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<std::string> ReadStringField(const JsonValue& root,
                                           std::initializer_list<std::string_view> canonical_path,
                                           std::initializer_list<std::string_view> legacy_path) {
  const JsonValue* value = FindScenarioField(root, canonical_path, legacy_path);
  if (value == nullptr || value->type != JsonValue::Type::kString) {
    return std::nullopt;
  }
  return value->string_value;
}

bool ParseRoi(const JsonValue& root, std::optional<ScenarioModel::Roi>& roi, std::string& error) {
  roi.reset();
  const JsonValue* roi_value = FindScenarioField(root, {"camera", "roi"}, {"roi"});
  if (roi_value == nullptr) {
    return true;
  }

  if (roi_value->type != JsonValue::Type::kObject) {
    error = "scenario camera.roi must include x, y, width, and height";
    return false;
  }

  auto read_required_non_negative_integer = [&](std::string_view key,
                                                std::uint64_t& target) -> bool {
    const JsonValue* value = FindObjectMember(*roi_value, key);
    if (value == nullptr) {
      return false;
    }
    return TryGetNonNegativeInteger(*value, target);
  };

  ScenarioModel::Roi parsed;
  if (!read_required_non_negative_integer("x", parsed.x) ||
      !read_required_non_negative_integer("y", parsed.y) ||
      !read_required_non_negative_integer("width", parsed.width) ||
      !read_required_non_negative_integer("height", parsed.height)) {
    error = "scenario camera.roi must include x, y, width, and height";
    return false;
  }

  roi = parsed;
  return true;
}

bool ParseScenarioModelRoot(const JsonValue& root, ScenarioModel& model, std::string& error) {
  model = ScenarioModel{};
  error.clear();

  model.duration.duration_ms = ReadU64Field(root, {"duration", "duration_ms"}, {"duration_ms"});
  model.duration.duration_s = ReadU64Field(root, {"duration", "duration_s"}, {"duration_s"});

  model.backend = ReadStringField(root, {"backend"}, {});
  model.apply_mode = ReadStringField(root, {"apply_mode"}, {});

  model.camera.fps = ReadU64Field(root, {"camera", "fps"}, {"fps"});
  model.camera.frame_size_bytes =
      ReadU64Field(root, {"camera", "frame_size_bytes"}, {"frame_size_bytes"});
  model.camera.pixel_format = ReadStringField(root, {"camera", "pixel_format"}, {"pixel_format"});
  model.camera.exposure_us = ReadU64Field(root, {"camera", "exposure_us"}, {"exposure_us"});
  model.camera.gain_db = ReadNumberField(root, {"camera", "gain_db"}, {"gain_db"});
  model.camera.packet_size_bytes =
      ReadU64Field(root, {"camera", "network", "packet_size_bytes"}, {"packet_size_bytes"});
  model.camera.inter_packet_delay_us =
      ReadU64Field(root, {"camera", "network", "inter_packet_delay_us"}, {"inter_packet_delay_us"});
  model.camera.trigger_mode = ReadStringField(root, {"camera", "trigger_mode"}, {"trigger_mode"});
  model.camera.trigger_source =
      ReadStringField(root, {"camera", "trigger_source"}, {"trigger_source"});
  model.camera.trigger_activation =
      ReadStringField(root, {"camera", "trigger_activation"}, {"trigger_activation"});

  if (!ParseRoi(root, model.camera.roi, error)) {
    return false;
  }

  model.sim_faults.seed = ReadU64Field(root, {"sim_faults", "seed"}, {"seed"});
  model.sim_faults.jitter_us = ReadU64Field(root, {"sim_faults", "jitter_us"}, {"jitter_us"});
  model.sim_faults.drop_every_n =
      ReadU64Field(root, {"sim_faults", "drop_every_n"}, {"drop_every_n"});
  model.sim_faults.drop_percent =
      ReadU64Field(root, {"sim_faults", "drop_percent"}, {"drop_percent"});
  model.sim_faults.burst_drop = ReadU64Field(root, {"sim_faults", "burst_drop"}, {"burst_drop"});
  model.sim_faults.reorder = ReadU64Field(root, {"sim_faults", "reorder"}, {"reorder"});

  model.thresholds.min_avg_fps =
      ReadNumberField(root, {"thresholds", "min_avg_fps"}, {"min_avg_fps"});
  model.thresholds.max_drop_rate_percent =
      ReadNumberField(root, {"thresholds", "max_drop_rate_percent"}, {"max_drop_rate_percent"});
  model.thresholds.max_inter_frame_interval_p95_us = ReadNumberField(
      root, {"thresholds", "max_inter_frame_interval_p95_us"}, {"max_inter_frame_interval_p95_us"});
  model.thresholds.max_inter_frame_jitter_p95_us = ReadNumberField(
      root, {"thresholds", "max_inter_frame_jitter_p95_us"}, {"max_inter_frame_jitter_p95_us"});
  model.thresholds.max_disconnect_count =
      ReadNumberField(root, {"thresholds", "max_disconnect_count"}, {"max_disconnect_count"});

  // Webcam-specific optional section.
  //
  // Parsing remains lenient by design: type mismatches in optional fields are
  // treated as unset values so runtime loading behavior stays backward
  // compatible while validator remains the strict schema gate.
  model.webcam.requested_width =
      ReadU64Field(root, {"webcam", "requested_width"}, {"requested_width"});
  model.webcam.requested_height =
      ReadU64Field(root, {"webcam", "requested_height"}, {"requested_height"});
  model.webcam.requested_fps =
      ReadNumberField(root, {"webcam", "requested_fps"}, {"requested_fps"});
  model.webcam.requested_pixel_format =
      ReadStringField(root, {"webcam", "requested_pixel_format"}, {"requested_pixel_format"});

  const JsonValue* webcam_selector = FindScenarioField(root, {"webcam", "device_selector"}, {});
  model.webcam.device_selector.reset();
  if (webcam_selector != nullptr && webcam_selector->type == JsonValue::Type::kObject) {
    ScenarioModel::Webcam::DeviceSelector parsed_selector;
    if (const JsonValue* index_value = FindObjectMember(*webcam_selector, "index");
        index_value != nullptr) {
      std::uint64_t parsed_index = 0;
      if (TryGetNonNegativeInteger(*index_value, parsed_index)) {
        parsed_selector.index = parsed_index;
      }
    }
    if (const JsonValue* id_value = FindObjectMember(*webcam_selector, "id");
        id_value != nullptr && id_value->type == JsonValue::Type::kString) {
      parsed_selector.id = id_value->string_value;
    }
    if (const JsonValue* name_contains_value = FindObjectMember(*webcam_selector, "name_contains");
        name_contains_value != nullptr && name_contains_value->type == JsonValue::Type::kString) {
      parsed_selector.name_contains = name_contains_value->string_value;
    }

    if (parsed_selector.index.has_value() || parsed_selector.id.has_value() ||
        parsed_selector.name_contains.has_value()) {
      model.webcam.device_selector = std::move(parsed_selector);
    }
  }

  model.netem_profile = ReadStringField(root, {"netem_profile"}, {});
  model.device_selector = ReadStringField(root, {"device_selector"}, {});

  return true;
}

} // namespace

bool ParseScenarioModelText(std::string_view json_text, ScenarioModel& model, std::string& error) {
  model = ScenarioModel{};
  error.clear();

  JsonValue root;
  JsonParser parser(json_text);
  std::string parse_error;
  if (!parser.Parse(root, parse_error)) {
    error = "invalid scenario JSON: " + parse_error;
    return false;
  }
  if (root.type != JsonValue::Type::kObject) {
    error = "scenario root must be a JSON object";
    return false;
  }

  return ParseScenarioModelRoot(root, model, error);
}

bool LoadScenarioModelFile(const std::string& scenario_path, ScenarioModel& model,
                           std::string& error) {
  std::ifstream file(fs::path(scenario_path), std::ios::binary);
  if (!file) {
    error = "unable to read scenario file: " + scenario_path;
    return false;
  }

  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  return ParseScenarioModelText(contents, model, error);
}

} // namespace labops::scenarios
