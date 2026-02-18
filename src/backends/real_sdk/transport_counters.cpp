#include "backends/real_sdk/transport_counters.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <span>
#include <string_view>

namespace labops::backends::real_sdk {

namespace {

std::string Trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool TryParseUnsigned(std::string_view text, std::uint64_t& value) {
  const std::string trimmed = Trim(text);
  if (trimmed.empty()) {
    return false;
  }
  const char* begin = trimmed.data();
  const char* end = begin + trimmed.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  return ec == std::errc() && ptr == end;
}

TransportCounterReading ResolveCounter(const BackendConfig& backend_dump,
                                       std::span<const std::string_view> aliases) {
  for (const auto& [key, raw_value] : backend_dump) {
    const std::string lowered_key = ToLowerAscii(key);
    for (const std::string_view alias : aliases) {
      if (lowered_key != alias) {
        continue;
      }
      std::uint64_t parsed_value = 0;
      if (!TryParseUnsigned(raw_value, parsed_value)) {
        continue;
      }

      return TransportCounterReading{
          .available = true,
          .value = parsed_value,
          .source_key = key,
      };
    }
  }

  return {};
}

constexpr std::array<std::string_view, 7> kResendAliases = {
    "transport.resends", "transport_resends", "device.transport_resends", "gevresendpacketcount",
    "gevresendcount",    "streamresendcount", "resendpacketcount",
};

constexpr std::array<std::string_view, 7> kPacketErrorAliases = {
    "transport.packet_errors", "transport_packet_errors", "device.transport_packet_errors",
    "gevpacketerrorcount",     "streampacketerrorcount",  "packeterrorcount",
    "transporterrorcount",
};

constexpr std::array<std::string_view, 7> kDroppedPacketAliases = {
    "transport.dropped_packets", "transport_dropped_packets", "device.transport_dropped_packets",
    "gevdroppedpacketcount",     "streamdroppedpacketcount",  "droppedpacketcount",
    "transportdroppedcount",
};

} // namespace

TransportCountersSnapshot CollectTransportCounters(const BackendConfig& backend_dump) {
  TransportCountersSnapshot snapshot;
  snapshot.resends = ResolveCounter(backend_dump, kResendAliases);
  snapshot.packet_errors = ResolveCounter(backend_dump, kPacketErrorAliases);
  snapshot.dropped_packets = ResolveCounter(backend_dump, kDroppedPacketAliases);
  return snapshot;
}

} // namespace labops::backends::real_sdk
