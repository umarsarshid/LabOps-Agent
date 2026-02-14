#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace labops::hostprobe {

// Parsed per-interface details extracted from raw NIC command output.
struct NicInterfaceHighlight {
  std::string name;
  std::optional<std::string> mac_address;
  std::vector<std::string> ipv4_addresses;
  std::vector<std::string> ipv6_addresses;
  std::optional<std::uint32_t> mtu_hint;
  std::optional<std::string> link_speed_hint;
  bool has_default_route = false;
};

// Parsed NIC overview included in `hostprobe.json`.
struct NicHighlights {
  std::optional<std::string> default_route_interface;
  std::vector<NicInterfaceHighlight> interfaces;
};

// One raw network command capture that will be written as a text artifact.
struct NicCommandCapture {
  std::string file_name;
  std::string command;
  int exit_code = -1;
  bool command_available = true;
  std::string output;
};

// Network probe result that includes both raw command output and parsed highlights.
struct NicProbeSnapshot {
  NicHighlights highlights;
  std::vector<NicCommandCapture> raw_captures;
};

// Redaction token set built from host/user identifiers.
struct IdentifierRedactionContext {
  std::vector<std::string> hostname_tokens;
  std::vector<std::string> username_tokens;
};

// Snapshot of host state captured near run start. This intentionally keeps only
// lightweight fields that are broadly available across developer and CI hosts.
struct HostProbeSnapshot {
  std::chrono::system_clock::time_point captured_at{};
  std::string os_name;
  std::string os_version;
  std::string cpu_model;
  std::uint32_t cpu_logical_cores = 0;
  std::uint64_t ram_total_bytes = 0;
  std::uint64_t uptime_seconds = 0;
  std::optional<double> load_avg_1m;
  std::optional<double> load_avg_5m;
  std::optional<double> load_avg_15m;
  NicHighlights nic_highlights;
};

// Collects a best-effort host snapshot. Missing platform fields are left with
// sensible defaults (`unknown` strings, zero counts, null load averages).
//
// Returns false only for hard failures; unsupported fields are not failures.
bool CollectHostProbeSnapshot(HostProbeSnapshot& snapshot, std::string& error);

// Collects raw NIC command outputs and parses highlights from those outputs.
//
// Command collection is best-effort:
// - unsupported or missing commands are recorded with `command_available=false`
// - this function still returns true unless a hard internal failure occurs.
bool CollectNicProbeSnapshot(NicProbeSnapshot& snapshot, std::string& error);

// Builds a best-effort token context used by `--redact` to strip obvious host
// and user identifiers from generated evidence.
void BuildIdentifierRedactionContext(IdentifierRedactionContext& context);

// Applies identifier redaction to parsed host probe highlights.
void RedactHostProbeSnapshot(HostProbeSnapshot& snapshot,
                             const IdentifierRedactionContext& context);

// Applies identifier redaction to raw NIC command captures.
void RedactNicProbeSnapshot(NicProbeSnapshot& snapshot,
                            const IdentifierRedactionContext& context);

// Serializes the snapshot to stable JSON suitable for artifact emission.
std::string ToJson(const HostProbeSnapshot& snapshot);

} // namespace labops::hostprobe
