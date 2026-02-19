#include "scenarios/validator.hpp"

#include "backends/real_sdk/real_backend_factory.hpp"
#include "core/json_dom.hpp"
#include "scenarios/netem_profile_support.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace labops::scenarios {

namespace {

using JsonValue = core::json::Value;
using JsonParser = core::json::Parser;

void AddIssue(ValidationReport& report, std::string path, std::string message) {
  report.issues.push_back({.path = std::move(path), .message = std::move(message)});
}

bool IsObject(const JsonValue* value) {
  return value != nullptr && value->type == JsonValue::Type::kObject;
}

bool IsArray(const JsonValue* value) {
  return value != nullptr && value->type == JsonValue::Type::kArray;
}

bool IsString(const JsonValue* value) {
  return value != nullptr && value->type == JsonValue::Type::kString;
}

bool IsNumber(const JsonValue* value) {
  return value != nullptr && value->type == JsonValue::Type::kNumber;
}

bool IsBool(const JsonValue* value) {
  return value != nullptr && value->type == JsonValue::Type::kBool;
}

const JsonValue* GetField(const JsonValue& object_value, std::string_view key) {
  if (object_value.type != JsonValue::Type::kObject) {
    return nullptr;
  }
  const auto it = object_value.object_value.find(std::string(key));
  if (it == object_value.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

bool TryGetNonNegativeInteger(const JsonValue& value, std::uint64_t& out) {
  if (value.type != JsonValue::Type::kNumber) {
    return false;
  }
  if (!std::isfinite(value.number_value) || value.number_value < 0.0) {
    return false;
  }
  const double floored = std::floor(value.number_value);
  if (floored != value.number_value) {
    return false;
  }
  if (floored > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return false;
  }
  out = static_cast<std::uint64_t>(floored);
  return true;
}

std::string Trim(std::string_view raw) {
  std::size_t begin = 0;
  while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
    ++begin;
  }

  std::size_t end = raw.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1])) != 0) {
    --end;
  }
  return std::string(raw.substr(begin, end - begin));
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

void ValidateRequiredString(const JsonValue& root, std::string_view key, std::string path,
                            std::string hint, ValidationReport& report) {
  const JsonValue* field = GetField(root, key);
  if (field == nullptr) {
    AddIssue(report, path, "is required; " + hint);
    return;
  }
  if (!IsString(field)) {
    AddIssue(report, path, "must be a string");
    return;
  }
  if (field->string_value.empty()) {
    AddIssue(report, path, "must not be empty");
  }
}

void ValidateDuration(const JsonValue& root, ValidationReport& report) {
  const JsonValue* duration = GetField(root, "duration");
  if (duration == nullptr) {
    AddIssue(report, "duration", "is required and must include duration.duration_ms");
    return;
  }
  if (!IsObject(duration)) {
    AddIssue(report, "duration", "must be an object with duration_ms");
    return;
  }

  const JsonValue* duration_ms = GetField(*duration, "duration_ms");
  if (duration_ms == nullptr) {
    AddIssue(report, "duration.duration_ms", "is required and must be > 0");
    return;
  }

  std::uint64_t parsed = 0;
  if (!TryGetNonNegativeInteger(*duration_ms, parsed)) {
    AddIssue(report, "duration.duration_ms", "must be a positive integer (milliseconds)");
    return;
  }
  if (parsed == 0U) {
    AddIssue(report, "duration.duration_ms", "must be greater than 0");
  }
}

void ValidateCamera(const JsonValue& root, ValidationReport& report) {
  const JsonValue* camera = GetField(root, "camera");
  if (camera == nullptr) {
    AddIssue(report, "camera", "is required");
    return;
  }
  if (!IsObject(camera)) {
    AddIssue(report, "camera", "must be an object");
    return;
  }

  if (const JsonValue* fps = GetField(*camera, "fps"); fps != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*fps, parsed) || parsed == 0U) {
      AddIssue(report, "camera.fps", "must be a positive integer");
    }
  }

  if (const JsonValue* width = GetField(*camera, "width"); width != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*width, parsed) || parsed == 0U) {
      AddIssue(report, "camera.width", "must be a positive integer");
    }
  }

  if (const JsonValue* height = GetField(*camera, "height"); height != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*height, parsed) || parsed == 0U) {
      AddIssue(report, "camera.height", "must be a positive integer");
    }
  }

  if (const JsonValue* exposure_us = GetField(*camera, "exposure_us"); exposure_us != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*exposure_us, parsed)) {
      AddIssue(report, "camera.exposure_us", "must be a non-negative integer");
    }
  }

  if (const JsonValue* trigger_mode = GetField(*camera, "trigger_mode"); trigger_mode != nullptr) {
    if (!IsString(trigger_mode)) {
      AddIssue(report, "camera.trigger_mode", "must be a string");
    } else {
      const std::string& mode = trigger_mode->string_value;
      if (mode != "free_run" && mode != "software" && mode != "hardware") {
        AddIssue(report, "camera.trigger_mode", "must be one of: free_run, software, hardware");
      }
    }
  }

  if (const JsonValue* trigger_source = GetField(*camera, "trigger_source");
      trigger_source != nullptr) {
    if (!IsString(trigger_source)) {
      AddIssue(report, "camera.trigger_source", "must be a string");
    } else {
      const std::string& source = trigger_source->string_value;
      if (source != "line0" && source != "line1" && source != "software") {
        AddIssue(report, "camera.trigger_source", "must be one of: line0, line1, software");
      }
    }
  }

  if (const JsonValue* trigger_activation = GetField(*camera, "trigger_activation");
      trigger_activation != nullptr) {
    if (!IsString(trigger_activation)) {
      AddIssue(report, "camera.trigger_activation", "must be a string");
    } else {
      const std::string& activation = trigger_activation->string_value;
      if (activation != "rising_edge" && activation != "falling_edge" && activation != "any_edge") {
        AddIssue(report, "camera.trigger_activation",
                 "must be one of: rising_edge, falling_edge, any_edge");
      }
    }
  }

  if (const JsonValue* roi = GetField(*camera, "roi"); roi != nullptr) {
    if (!IsObject(roi)) {
      AddIssue(report, "camera.roi", "must be an object");
    } else {
      auto validate_non_negative = [&](std::string_view field) {
        const JsonValue* value = GetField(*roi, field);
        if (value == nullptr) {
          AddIssue(report, "camera.roi." + std::string(field), "is required when roi is present");
          return;
        }
        std::uint64_t parsed = 0;
        if (!TryGetNonNegativeInteger(*value, parsed)) {
          AddIssue(report, "camera.roi." + std::string(field), "must be a non-negative integer");
        }
      };

      auto validate_positive = [&](std::string_view field) {
        const JsonValue* value = GetField(*roi, field);
        if (value == nullptr) {
          AddIssue(report, "camera.roi." + std::string(field), "is required when roi is present");
          return;
        }
        std::uint64_t parsed = 0;
        if (!TryGetNonNegativeInteger(*value, parsed) || parsed == 0U) {
          AddIssue(report, "camera.roi." + std::string(field), "must be a positive integer");
        }
      };

      validate_non_negative("x");
      validate_non_negative("y");
      validate_positive("width");
      validate_positive("height");
    }
  }

  if (const JsonValue* network = GetField(*camera, "network"); network != nullptr) {
    if (!IsObject(network)) {
      AddIssue(report, "camera.network", "must be an object");
    } else {
      if (const JsonValue* packet_size = GetField(*network, "packet_size_bytes");
          packet_size != nullptr) {
        std::uint64_t parsed = 0;
        if (!TryGetNonNegativeInteger(*packet_size, parsed) || parsed == 0U) {
          AddIssue(report, "camera.network.packet_size_bytes", "must be a positive integer");
        }
      }
      if (const JsonValue* inter_packet_delay = GetField(*network, "inter_packet_delay_us");
          inter_packet_delay != nullptr) {
        std::uint64_t parsed = 0;
        if (!TryGetNonNegativeInteger(*inter_packet_delay, parsed)) {
          AddIssue(report, "camera.network.inter_packet_delay_us",
                   "must be a non-negative integer");
        }
      }
    }
  }
}

void ValidateTags(const JsonValue& root, ValidationReport& report) {
  const JsonValue* tags = GetField(root, "tags");
  if (tags == nullptr) {
    return;
  }
  if (!IsArray(tags)) {
    AddIssue(report, "tags", "must be an array of non-empty strings");
    return;
  }
  for (std::size_t i = 0; i < tags->array_value.size(); ++i) {
    const JsonValue& tag = tags->array_value[i];
    if (tag.type != JsonValue::Type::kString || tag.string_value.empty()) {
      AddIssue(report, "tags[" + std::to_string(i) + "]", "must be a non-empty string");
    }
  }
}

void ValidateSimFaults(const JsonValue& root, ValidationReport& report) {
  const JsonValue* sim_faults = GetField(root, "sim_faults");
  if (sim_faults == nullptr) {
    return;
  }
  if (!IsObject(sim_faults)) {
    AddIssue(report, "sim_faults", "must be an object");
    return;
  }

  auto validate_non_negative_integer = [&](std::string_view field) {
    const JsonValue* value = GetField(*sim_faults, field);
    if (value == nullptr) {
      return;
    }
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*value, parsed)) {
      AddIssue(report, "sim_faults." + std::string(field), "must be a non-negative integer");
    }
  };

  validate_non_negative_integer("seed");
  validate_non_negative_integer("jitter_us");
  validate_non_negative_integer("drop_every_n");
  validate_non_negative_integer("burst_drop");
  validate_non_negative_integer("reorder");
  validate_non_negative_integer("disconnect_at_ms");
  validate_non_negative_integer("disconnect_duration_ms");

  if (const JsonValue* drop_percent = GetField(*sim_faults, "drop_percent");
      drop_percent != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*drop_percent, parsed)) {
      AddIssue(report, "sim_faults.drop_percent", "must be an integer in range [0,100]");
    } else if (parsed > 100U) {
      AddIssue(report, "sim_faults.drop_percent", "must be in range [0,100]");
    }
  }
}

void ValidateThresholds(const JsonValue& root, ValidationReport& report) {
  const JsonValue* thresholds = GetField(root, "thresholds");
  if (thresholds == nullptr) {
    AddIssue(report, "thresholds", "is required");
    return;
  }
  if (!IsObject(thresholds)) {
    AddIssue(report, "thresholds", "must be an object");
    return;
  }

  bool has_known_threshold = false;

  auto validate_non_negative_number = [&](std::string_view field, bool percent_0_to_100 = false) {
    const JsonValue* value = GetField(*thresholds, field);
    if (value == nullptr) {
      return;
    }
    has_known_threshold = true;
    if (!IsNumber(value) || !std::isfinite(value->number_value) || value->number_value < 0.0) {
      AddIssue(report, "thresholds." + std::string(field), "must be a non-negative number");
      return;
    }
    if (percent_0_to_100 && value->number_value > 100.0) {
      AddIssue(report, "thresholds." + std::string(field), "must be in range [0,100]");
    }
  };

  auto validate_non_negative_integer = [&](std::string_view field) {
    const JsonValue* value = GetField(*thresholds, field);
    if (value == nullptr) {
      return;
    }
    has_known_threshold = true;
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*value, parsed)) {
      AddIssue(report, "thresholds." + std::string(field), "must be a non-negative integer");
    }
  };

  validate_non_negative_number("min_avg_fps");
  validate_non_negative_number("max_drop_rate_percent", true);
  validate_non_negative_number("max_inter_frame_interval_p95_us");
  validate_non_negative_number("max_inter_frame_jitter_p95_us");
  validate_non_negative_integer("max_disconnect_count");

  if (!has_known_threshold) {
    AddIssue(report, "thresholds",
             "must include at least one threshold (e.g. max_drop_rate_percent)");
  }
}

void ValidateOaat(const JsonValue& root, ValidationReport& report) {
  const JsonValue* oaat = GetField(root, "oaat");
  if (oaat == nullptr) {
    return;
  }
  if (!IsObject(oaat)) {
    AddIssue(report, "oaat", "must be an object");
    return;
  }

  const JsonValue* enabled = GetField(*oaat, "enabled");
  if (enabled == nullptr) {
    AddIssue(report, "oaat.enabled", "is required when oaat is present");
    return;
  }
  if (!IsBool(enabled)) {
    AddIssue(report, "oaat.enabled", "must be a boolean");
    return;
  }

  if (const JsonValue* max_trials = GetField(*oaat, "max_trials"); max_trials != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*max_trials, parsed) || parsed == 0U) {
      AddIssue(report, "oaat.max_trials", "must be a positive integer");
    }
  }

  if (const JsonValue* stop_on_first_failure = GetField(*oaat, "stop_on_first_failure");
      stop_on_first_failure != nullptr) {
    if (!IsBool(stop_on_first_failure)) {
      AddIssue(report, "oaat.stop_on_first_failure", "must be a boolean");
    }
  }

  const JsonValue* variables = GetField(*oaat, "variables");
  if (enabled->bool_value &&
      (variables == nullptr || !IsArray(variables) || variables->array_value.empty())) {
    AddIssue(report, "oaat.variables", "must contain at least one variable when oaat.enabled=true");
    return;
  }

  if (variables == nullptr) {
    return;
  }
  if (!IsArray(variables)) {
    AddIssue(report, "oaat.variables", "must be an array");
    return;
  }

  for (std::size_t i = 0; i < variables->array_value.size(); ++i) {
    const JsonValue& variable = variables->array_value[i];
    const std::string base_path = "oaat.variables[" + std::to_string(i) + "]";
    if (variable.type != JsonValue::Type::kObject) {
      AddIssue(report, base_path, "must be an object");
      continue;
    }

    const JsonValue* path = GetField(variable, "path");
    if (path == nullptr || !IsString(path) || path->string_value.empty()) {
      AddIssue(report, base_path + ".path", "is required and must be a non-empty string");
    }

    const JsonValue* values = GetField(variable, "values");
    if (values == nullptr || !IsArray(values) || values->array_value.empty()) {
      AddIssue(report, base_path + ".values", "is required and must be a non-empty array");
    }

    if (const JsonValue* mode = GetField(variable, "mode"); mode != nullptr) {
      if (!IsString(mode)) {
        AddIssue(report, base_path + ".mode", "must be a string when provided");
      } else if (mode->string_value != "replace") {
        AddIssue(report, base_path + ".mode", "must be 'replace' in current schema");
      }
    }
  }
}

void ValidateApplyMode(const JsonValue& root, ValidationReport& report) {
  const JsonValue* apply_mode = GetField(root, "apply_mode");
  if (apply_mode == nullptr) {
    return;
  }

  if (!IsString(apply_mode)) {
    AddIssue(report, "apply_mode", "must be a string when provided");
    return;
  }

  const std::string normalized = ToLower(Trim(apply_mode->string_value));
  if (normalized.empty()) {
    AddIssue(report, "apply_mode", "must not be empty when provided");
    return;
  }
  if (normalized != "strict" && normalized != "best_effort" && normalized != "best-effort") {
    AddIssue(report, "apply_mode", "must be one of: strict, best_effort");
  }
}

void ValidateNetemProfile(const JsonValue& root, const fs::path& scenario_path,
                          ValidationReport& report) {
  const JsonValue* profile = GetField(root, "netem_profile");
  if (profile == nullptr) {
    return;
  }

  if (!IsString(profile)) {
    AddIssue(report, "netem_profile", "must be a string profile id");
    return;
  }
  if (profile->string_value.empty()) {
    AddIssue(report, "netem_profile", "must not be empty when provided");
    return;
  }
  if (!IsLowercaseSlug(profile->string_value)) {
    AddIssue(report, "netem_profile", "must use lowercase slug format [a-z0-9_-]+");
    return;
  }

  if (scenario_path.empty()) {
    return;
  }

  fs::path resolved_profile_path;
  if (!ResolveNetemProfilePath(scenario_path, profile->string_value, resolved_profile_path)) {
    AddIssue(report, "netem_profile",
             "profile '" + profile->string_value +
                 "' was not found under tools/netem_profiles/<profile>.json");
  }
}

void ValidateBackend(const JsonValue& root, ValidationReport& report) {
  const JsonValue* backend = GetField(root, "backend");
  if (backend == nullptr) {
    return;
  }

  if (!IsString(backend)) {
    AddIssue(report, "backend", "must be a string when provided");
    return;
  }
  if (backend->string_value.empty()) {
    AddIssue(report, "backend", "must not be empty when provided");
    return;
  }
  if (backend->string_value != "sim" && backend->string_value != "webcam" &&
      backend->string_value != "real_stub") {
    AddIssue(report, "backend", "must be one of: sim, webcam, real_stub");
  }
}

void ValidateDeviceSelector(const JsonValue& root, ValidationReport& report) {
  const JsonValue* selector = GetField(root, "device_selector");
  if (selector == nullptr) {
    return;
  }

  if (!IsString(selector)) {
    AddIssue(report, "device_selector", "must be a string when provided");
    return;
  }

  const std::string selector_text = Trim(selector->string_value);
  if (selector_text.empty()) {
    AddIssue(report, "device_selector", "must not be empty when provided");
    return;
  }
  // Reuse the runtime parser so `labops validate` and `labops run` enforce the
  // exact same selector syntax and key rules.
  backends::real_sdk::DeviceSelector parsed_selector;
  std::string parse_error;
  if (!backends::real_sdk::ParseDeviceSelector(selector_text, parsed_selector, parse_error)) {
    AddIssue(report, "device_selector", parse_error);
    return;
  }

  const JsonValue* backend = GetField(root, "backend");
  const std::string backend_value =
      (backend != nullptr && IsString(backend) && !backend->string_value.empty())
          ? backend->string_value
          : "sim";
  if (backend_value != "real_stub") {
    AddIssue(report, "device_selector", "requires backend to be \"real_stub\"");
  }
}

void ValidateWebcam(const JsonValue& root, ValidationReport& report) {
  const JsonValue* webcam = GetField(root, "webcam");
  if (webcam == nullptr) {
    return;
  }
  if (!IsObject(webcam)) {
    AddIssue(report, "webcam", "must be an object when provided");
    return;
  }

  if (const JsonValue* requested_width = GetField(*webcam, "requested_width");
      requested_width != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*requested_width, parsed) || parsed == 0U) {
      AddIssue(report, "webcam.requested_width", "must be a positive integer");
    }
  }

  if (const JsonValue* requested_height = GetField(*webcam, "requested_height");
      requested_height != nullptr) {
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*requested_height, parsed) || parsed == 0U) {
      AddIssue(report, "webcam.requested_height", "must be a positive integer");
    }
  }

  if (const JsonValue* requested_fps = GetField(*webcam, "requested_fps");
      requested_fps != nullptr) {
    if (!IsNumber(requested_fps) || !std::isfinite(requested_fps->number_value) ||
        requested_fps->number_value <= 0.0) {
      AddIssue(report, "webcam.requested_fps", "must be a positive number");
    }
  }

  if (const JsonValue* requested_pixel_format = GetField(*webcam, "requested_pixel_format");
      requested_pixel_format != nullptr) {
    if (!IsString(requested_pixel_format) || requested_pixel_format->string_value.empty()) {
      AddIssue(report, "webcam.requested_pixel_format", "must be a non-empty string");
    }
  }

  const JsonValue* device_selector = GetField(*webcam, "device_selector");
  if (device_selector == nullptr) {
    return;
  }
  if (!IsObject(device_selector)) {
    AddIssue(report, "webcam.device_selector", "must be an object when provided");
    return;
  }

  bool has_selector_field = false;
  if (const JsonValue* index = GetField(*device_selector, "index"); index != nullptr) {
    has_selector_field = true;
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*index, parsed)) {
      AddIssue(report, "webcam.device_selector.index", "must be a non-negative integer");
    }
  }

  if (const JsonValue* id = GetField(*device_selector, "id"); id != nullptr) {
    has_selector_field = true;
    if (!IsString(id) || id->string_value.empty()) {
      AddIssue(report, "webcam.device_selector.id", "must be a non-empty string");
    }
  }

  if (const JsonValue* name_contains = GetField(*device_selector, "name_contains");
      name_contains != nullptr) {
    has_selector_field = true;
    if (!IsString(name_contains) || name_contains->string_value.empty()) {
      AddIssue(report, "webcam.device_selector.name_contains", "must be a non-empty string");
    }
  }

  if (!has_selector_field) {
    AddIssue(report, "webcam.device_selector",
             "must include at least one selector key: index, id, or name_contains");
  }
}

void ValidateScenarioObject(const JsonValue& root, const fs::path& scenario_path,
                            ValidationReport& report) {
  if (root.type != JsonValue::Type::kObject) {
    AddIssue(report, "$", "root JSON value must be an object");
    return;
  }

  ValidateRequiredString(root, "schema_version", "schema_version", "example: \"1.0\"", report);

  ValidateRequiredString(root, "scenario_id", "scenario_id", "example: \"stream_baseline_1080p\"",
                         report);

  if (const JsonValue* scenario_id = GetField(root, "scenario_id");
      IsString(scenario_id) && !scenario_id->string_value.empty()) {
    if (!IsLowercaseSlug(scenario_id->string_value)) {
      AddIssue(report, "scenario_id", "must use lowercase slug format [a-z0-9_-]+");
    }
  }

  if (const JsonValue* description = GetField(root, "description"); description != nullptr) {
    if (!IsString(description)) {
      AddIssue(report, "description", "must be a string");
    }
  }

  ValidateTags(root, report);
  ValidateDuration(root, report);
  ValidateCamera(root, report);
  ValidateSimFaults(root, report);
  ValidateThresholds(root, report);
  ValidateOaat(root, report);
  ValidateApplyMode(root, report);
  ValidateNetemProfile(root, scenario_path, report);
  ValidateBackend(root, report);
  ValidateDeviceSelector(root, report);
  ValidateWebcam(root, report);
}

} // namespace

bool ValidateScenarioText(std::string_view json_text, ValidationReport& report,
                          std::string& error) {
  report = ValidationReport{};

  JsonValue root;
  JsonParser parser(json_text);
  std::string parse_error;
  if (!parser.Parse(root, parse_error)) {
    AddIssue(report, "$",
             parse_error + " (fix JSON syntax and rerun 'labops validate <scenario.json>')");
    report.valid = false;
    return true;
  }

  ValidateScenarioObject(root, fs::path{}, report);
  report.valid = report.issues.empty();
  return true;
}

bool ValidateScenarioFile(const std::string& scenario_path, ValidationReport& report,
                          std::string& error) {
  std::ifstream file(fs::path(scenario_path), std::ios::binary);
  if (!file) {
    error = "unable to read scenario file: " + scenario_path;
    return false;
  }

  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  if (contents.empty()) {
    report = ValidationReport{};
    AddIssue(report, "$", "scenario file is empty; provide a valid JSON object");
    report.valid = false;
    return true;
  }

  JsonValue root;
  JsonParser parser(contents);
  std::string parse_error;
  if (!parser.Parse(root, parse_error)) {
    report = ValidationReport{};
    AddIssue(report, "$",
             parse_error + " (fix JSON syntax and rerun 'labops validate <scenario.json>')");
    report.valid = false;
    return true;
  }

  report = ValidationReport{};
  ValidateScenarioObject(root, fs::path(scenario_path), report);
  report.valid = report.issues.empty();
  return true;
}

} // namespace labops::scenarios
