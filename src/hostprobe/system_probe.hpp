#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace labops::hostprobe {

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
};

// Collects a best-effort host snapshot. Missing platform fields are left with
// sensible defaults (`unknown` strings, zero counts, null load averages).
//
// Returns false only for hard failures; unsupported fields are not failures.
bool CollectHostProbeSnapshot(HostProbeSnapshot& snapshot, std::string& error);

// Serializes the snapshot to stable JSON suitable for artifact emission.
std::string ToJson(const HostProbeSnapshot& snapshot);

} // namespace labops::hostprobe
