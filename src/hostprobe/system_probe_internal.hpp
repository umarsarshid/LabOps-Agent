#pragma once

#include "hostprobe/system_probe.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace labops::hostprobe::internal {

// Shared helpers that keep platform-specific probe units small and consistent.
void AddIdentifierTokenAndVariants(std::vector<std::string>& out, const std::string& token);

NicCommandCapture CaptureCommand(const std::string& file_name, const std::string& command);
bool IsCommandAvailable(const std::string& command_name);

NicInterfaceHighlight& GetOrCreateInterface(NicHighlights& highlights,
                                            const std::string& interface_name);
void SortInterfaces(NicHighlights& highlights);
void CollectUnsupportedPlatformNicProbe(NicProbeSnapshot& snapshot);

void ParseLinuxIpAddressOutput(const std::string& output, NicHighlights& highlights);
void ParseLinuxRouteOutput(const std::string& output, NicHighlights& highlights);
std::optional<std::string> ParseLinuxEthtoolSpeedHint(const std::string& output);

void ParseMacIfconfigOutput(const std::string& output, NicHighlights& highlights);
void ParseMacRouteGetDefaultOutput(const std::string& output, NicHighlights& highlights);
void ParseMacNetstatRouteOutput(const std::string& output, NicHighlights& highlights);

void ParseWindowsIpconfigOutput(const std::string& output, NicHighlights& highlights);

// Platform hook points consumed by the shared hostprobe pipeline.
void AddSystemHostnameTokensPlatform(std::vector<std::string>& out);
std::string DetectOsVersionPlatform();
std::string ProbeCpuModelPlatform();
std::uint64_t ProbeRamTotalBytesPlatform();
std::uint64_t ProbeUptimeSecondsPlatform();
std::array<std::optional<double>, 3> ProbeLoadAveragesPlatform();
void CollectNicProbePlatform(NicProbeSnapshot& snapshot);

} // namespace labops::hostprobe::internal
