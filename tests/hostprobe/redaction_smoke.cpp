#include "hostprobe/system_probe.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

std::optional<std::string> ReadEnvVar(const char* name) {
  if (name == nullptr) {
    return std::nullopt;
  }
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

void SetEnvVar(const char* name, const char* value) {
#if defined(_WIN32)
  if (_putenv_s(name, value) != 0) {
    Fail("failed to set environment variable");
  }
#else
  if (setenv(name, value, 1) != 0) {
    Fail("failed to set environment variable");
  }
#endif
}

void UnsetEnvVar(const char* name) {
#if defined(_WIN32)
  if (_putenv_s(name, "") != 0) {
    Fail("failed to unset environment variable");
  }
#else
  if (unsetenv(name) != 0) {
    Fail("failed to unset environment variable");
  }
#endif
}

class ScopedEnvOverride {
public:
  ScopedEnvOverride(const char* name, const char* value)
      : name_(name), previous_(ReadEnvVar(name)) {
    SetEnvVar(name_, value);
  }

  ~ScopedEnvOverride() {
    if (previous_.has_value()) {
      SetEnvVar(name_, previous_->c_str());
      return;
    }
    UnsetEnvVar(name_);
  }

  ScopedEnvOverride(const ScopedEnvOverride&) = delete;
  ScopedEnvOverride& operator=(const ScopedEnvOverride&) = delete;

private:
  const char* name_ = "";
  std::optional<std::string> previous_;
};

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find token: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

void AssertNotContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) != std::string_view::npos) {
    std::cerr << "unexpected token leaked: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  constexpr const char* kHostToken = "ci-redact-host-01";
  constexpr const char* kUserToken = "ci_redact_user_01";

  // Force stable identifiers so the smoke test validates context-based
  // redaction deterministically on any host or CI runner.
  ScopedEnvOverride host_override("HOSTNAME", kHostToken);
  ScopedEnvOverride user_override("USER", kUserToken);

  labops::hostprobe::IdentifierRedactionContext context;
  labops::hostprobe::BuildIdentifierRedactionContext(context);

  labops::hostprobe::HostProbeSnapshot host_snapshot;
  host_snapshot.os_name = "linux-" + std::string(kHostToken);
  host_snapshot.os_version = "version-owned-by-" + std::string(kUserToken);
  host_snapshot.cpu_model = "cpu@" + std::string(kHostToken);
  host_snapshot.nic_highlights.default_route_interface = "route-via-" + std::string(kHostToken);
  host_snapshot.nic_highlights.interfaces.push_back(labops::hostprobe::NicInterfaceHighlight{});
  host_snapshot.nic_highlights.interfaces.back().name = "iface-" + std::string(kHostToken);
  host_snapshot.nic_highlights.interfaces.back().ipv4_addresses.push_back("owner-" +
                                                                          std::string(kUserToken));
  host_snapshot.nic_highlights.interfaces.back().link_speed_hint =
      "speed-for-" + std::string(kHostToken);

  labops::hostprobe::NicProbeSnapshot nic_snapshot;
  nic_snapshot.highlights = host_snapshot.nic_highlights;
  nic_snapshot.raw_captures.push_back(labops::hostprobe::NicCommandCapture{});
  nic_snapshot.raw_captures.back().file_name = "nic_ip_a.txt";
  nic_snapshot.raw_captures.back().command = "ip a #" + std::string(kUserToken);
  nic_snapshot.raw_captures.back().output = "Host Name: " + std::string(kHostToken) +
                                            "\n"
                                            "Profile Path: /Users/" +
                                            std::string(kUserToken) + "/workspace\n";

  labops::hostprobe::RedactHostProbeSnapshot(host_snapshot, context);
  labops::hostprobe::RedactNicProbeSnapshot(nic_snapshot, context);

  const std::string host_json = labops::hostprobe::ToJson(host_snapshot);
  AssertContains(host_json, "<redacted_host>");
  AssertContains(host_json, "<redacted_user>");
  AssertNotContains(host_json, kHostToken);
  AssertNotContains(host_json, kUserToken);

  const auto& capture = nic_snapshot.raw_captures.front();
  AssertContains(capture.output, "<redacted_host>");
  AssertContains(capture.output, "<redacted_user>");
  AssertNotContains(capture.output, kHostToken);
  AssertNotContains(capture.output, kUserToken);
  AssertNotContains(capture.command, kUserToken);

  return 0;
}
