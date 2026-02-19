#pragma once

#include "backends/webcam/device_model.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::webcam {

// Selector contract used by scenario `webcam.device_selector` and optional
// CLI overrides for webcam runs.
struct WebcamDeviceSelector {
  std::optional<std::string> id;
  std::optional<std::size_t> index;
  std::optional<std::string> name_contains;
};

// Resolution reason is captured so logs/reports can clearly explain why a
// specific webcam was selected (id match, index, name substring, or default).
enum class WebcamSelectionRule {
  kId = 0,
  kIndex,
  kNameContains,
  kDefaultDevice,
};

struct WebcamSelectionResult {
  WebcamDeviceInfo device;
  std::size_t index = 0;
  WebcamSelectionRule rule = WebcamSelectionRule::kDefaultDevice;
};

// Parses selector text in key:value clause format:
// - id:<value>
// - index:<n>
// - name_contains:<substring>
// Clauses can be comma-separated (for example: `id:cam-1,index:0`).
bool ParseWebcamDeviceSelector(std::string_view selector_text, WebcamDeviceSelector& selector,
                               std::string& error);

// Enumerates webcam devices from the OSS fixture source:
// - env var: LABOPS_WEBCAM_DEVICE_FIXTURE (CSV)
// - CSV columns: device_id,friendly_name[,bus_info]
//
// This keeps selector behavior testable in CI without requiring attached
// webcams. Future platform integrations can swap in OS-native enumeration
// while preserving the same output contract.
bool EnumerateConnectedDevices(std::vector<WebcamDeviceInfo>& devices, std::string& error);

// Resolves one webcam using deterministic rules:
// 1) if selector.id set: exact match
// 2) else if selector.index set: stable sorted index
// 3) else if selector.name_contains set: first case-insensitive name match
// 4) else: default index 0
bool ResolveWebcamDeviceSelector(const std::vector<WebcamDeviceInfo>& devices,
                                 const WebcamDeviceSelector& selector,
                                 WebcamSelectionResult& resolved, std::string& error);

const char* ToString(WebcamSelectionRule rule);

} // namespace labops::backends::webcam
