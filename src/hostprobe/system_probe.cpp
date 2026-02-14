#include "hostprobe/system_probe.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <thread>
#include <time.h>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace labops::hostprobe {

namespace {

std::string EscapeJson(std::string_view input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default: {
      const auto as_unsigned = static_cast<unsigned char>(ch);
      if (as_unsigned < 0x20U) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(as_unsigned) << std::dec << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
    }
  }
  return out.str();
}

std::string FormatUtcTimestamp(std::chrono::system_clock::time_point timestamp) {
  const auto millis_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
  const auto millis_component = static_cast<int>((millis_since_epoch % 1000 + 1000) % 1000);

  const std::time_t epoch_seconds = std::chrono::system_clock::to_time_t(timestamp);
  std::tm utc_time{};
#if defined(_WIN32)
  const errno_t result = gmtime_s(&utc_time, &epoch_seconds);
  if (result != 0) {
    return "";
  }
#else
  const std::tm* result = gmtime_r(&epoch_seconds, &utc_time);
  if (result == nullptr) {
    return "";
  }
#endif

  std::ostringstream out;
  out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis_component << 'Z';
  return out.str();
}

std::string FormatDouble(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::string ToLower(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return out;
}

std::string Trim(std::string_view value) {
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return std::string(value.substr(start, end - start));
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> SplitWhitespace(std::string_view line) {
  std::vector<std::string> tokens;
  std::string current;
  for (const char ch : line) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

void AddUnique(std::vector<std::string>& values, const std::string& value) {
  if (value.empty()) {
    return;
  }
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

std::string StripPrefixUntil(std::string_view value, char needle) {
  const std::size_t pos = value.find(needle);
  if (pos == std::string::npos || pos + 1U >= value.size()) {
    return "";
  }
  return Trim(value.substr(pos + 1U));
}

std::string FirstToken(std::string_view value) {
  const std::vector<std::string> tokens = SplitWhitespace(value);
  if (tokens.empty()) {
    return "";
  }
  return tokens.front();
}

std::string StripCidrSuffix(std::string value) {
  const std::size_t slash = value.find('/');
  if (slash != std::string::npos) {
    value = value.substr(0, slash);
  }
  return value;
}

std::string StripIpv6Zone(std::string value) {
  const std::size_t percent = value.find('%');
  if (percent != std::string::npos) {
    value = value.substr(0, percent);
  }
  return value;
}

std::string NormalizeInterfaceName(std::string value) {
  value = Trim(value);
  const std::size_t at = value.find('@');
  if (at != std::string::npos) {
    value = value.substr(0, at);
  }
  return value;
}

std::optional<std::uint32_t> ParseFirstUnsigned(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() && std::isdigit(static_cast<unsigned char>(text[start])) == 0) {
    ++start;
  }
  if (start == text.size()) {
    return std::nullopt;
  }

  std::size_t end = start;
  while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
    ++end;
  }

  std::uint64_t parsed = 0;
  const auto* begin = text.data() + static_cast<std::ptrdiff_t>(start);
  const auto* finish = text.data() + static_cast<std::ptrdiff_t>(end);
  const auto [ptr, ec] = std::from_chars(begin, finish, parsed);
  if (ec != std::errc() || ptr != finish ||
      parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(parsed);
}

std::optional<std::uint32_t> ExtractUnsignedAfterToken(std::string_view text,
                                                       std::string_view token) {
  const std::size_t pos = text.find(token);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return ParseFirstUnsigned(text.substr(pos + token.size()));
}

std::optional<std::string> NormalizeLinkSpeedHint(std::string value) {
  value = Trim(value);
  if (value.empty()) {
    return std::nullopt;
  }

  const std::string lower = ToLower(value);
  if (lower == "unknown" || lower == "unknown!" || lower == "n/a") {
    return std::nullopt;
  }

  bool has_digit = false;
  for (const char ch : lower) {
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      has_digit = true;
      break;
    }
  }

  const bool has_speed_unit =
      lower.find("mb/s") != std::string::npos || lower.find("mbps") != std::string::npos ||
      lower.find("gb/s") != std::string::npos || lower.find("gbps") != std::string::npos ||
      lower.find("tb/s") != std::string::npos || lower.find("tbps") != std::string::npos ||
      lower.find("base") != std::string::npos;
  if (!has_digit || !has_speed_unit) {
    return std::nullopt;
  }

  return value;
}

bool IsIdentifierChar(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool HasLeftBoundary(std::string_view text, std::size_t pos) {
  if (pos == 0U) {
    return true;
  }
  return !IsIdentifierChar(text[pos - 1U]);
}

bool HasRightBoundary(std::string_view text, std::size_t pos, std::size_t length) {
  const std::size_t end = pos + length;
  if (end >= text.size()) {
    return true;
  }
  return !IsIdentifierChar(text[end]);
}

bool IsLikelyGenericIdentifierToken(std::string_view token) {
  const std::string lower = ToLower(token);
  static const std::array<std::string_view, 8> kGenericTokens = {
      "unknown", "localhost", "localdomain", "default", "none", "n/a", "na", "user",
  };
  return std::find(kGenericTokens.begin(), kGenericTokens.end(), lower) != kGenericTokens.end();
}

std::string NormalizeIdentifierToken(std::string value) {
  value = Trim(value);
  while (!value.empty() && (value.front() == '"' || value.front() == '\'' || value.front() == '(' ||
                            value.front() == '[' || value.front() == '{')) {
    value.erase(value.begin());
    value = Trim(value);
  }
  while (!value.empty() && (value.back() == '"' || value.back() == '\'' || value.back() == ')' ||
                            value.back() == ']' || value.back() == '}' || value.back() == ',' ||
                            value.back() == ';' || value.back() == ':')) {
    value.pop_back();
    value = Trim(value);
  }
  return value;
}

std::string TailPathSegment(std::string_view path_text) {
  const std::size_t slash = path_text.find_last_of("/\\");
  if (slash == std::string::npos || slash + 1U >= path_text.size()) {
    return NormalizeIdentifierToken(std::string(path_text));
  }
  return NormalizeIdentifierToken(std::string(path_text.substr(slash + 1U)));
}

void AddNormalizedIdentifierToken(std::vector<std::string>& out, std::string token) {
  token = NormalizeIdentifierToken(std::move(token));
  if (token.size() < 3U || IsLikelyGenericIdentifierToken(token)) {
    return;
  }

  bool has_alpha = false;
  for (const char ch : token) {
    if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
      has_alpha = true;
      break;
    }
  }
  if (!has_alpha) {
    return;
  }

  const std::string normalized_lower = ToLower(token);
  for (const auto& existing : out) {
    if (ToLower(existing) == normalized_lower) {
      return;
    }
  }
  out.push_back(token);
}

void AddIdentifierTokenAndVariants(std::vector<std::string>& out, const std::string& token) {
  AddNormalizedIdentifierToken(out, token);

  // Hostnames may appear either as full FQDN or short host token in artifacts.
  const std::size_t dot = token.find('.');
  if (dot != std::string::npos) {
    AddNormalizedIdentifierToken(out, token.substr(0, dot));
  }
}

void ReplaceIdentifierToken(std::string& text, const std::string& token,
                            const std::string& replacement) {
  if (token.empty() || text.empty()) {
    return;
  }

  const std::string token_lower = ToLower(token);
  std::string text_lower = ToLower(text);

  std::size_t pos = 0;
  while (pos < text_lower.size()) {
    const std::size_t found = text_lower.find(token_lower, pos);
    if (found == std::string::npos) {
      break;
    }

    if (!HasLeftBoundary(text, found) || !HasRightBoundary(text, found, token.size())) {
      pos = found + token.size();
      continue;
    }

    text.replace(found, token.size(), replacement);
    text_lower.replace(found, token.size(), replacement);
    pos = found + replacement.size();
  }
}

void RedactStringValue(std::string& value, const IdentifierRedactionContext& context) {
  for (const auto& token : context.hostname_tokens) {
    ReplaceIdentifierToken(value, token, "<redacted_host>");
  }
  for (const auto& token : context.username_tokens) {
    ReplaceIdentifierToken(value, token, "<redacted_user>");
  }
}

void RedactStringOptional(std::optional<std::string>& value,
                          const IdentifierRedactionContext& context) {
  if (!value.has_value()) {
    return;
  }
  RedactStringValue(value.value(), context);
}

void RedactStringVector(std::vector<std::string>& values,
                        const IdentifierRedactionContext& context) {
  for (auto& entry : values) {
    RedactStringValue(entry, context);
  }
}

void RedactNicHighlights(NicHighlights& highlights, const IdentifierRedactionContext& context) {
  RedactStringOptional(highlights.default_route_interface, context);
  for (auto& iface : highlights.interfaces) {
    RedactStringValue(iface.name, context);
    RedactStringOptional(iface.mac_address, context);
    RedactStringVector(iface.ipv4_addresses, context);
    RedactStringVector(iface.ipv6_addresses, context);
    RedactStringOptional(iface.link_speed_hint, context);
  }
}

void AddEnvironmentToken(std::vector<std::string>& out, const char* env_name) {
  if (env_name == nullptr) {
    return;
  }
  const char* raw = std::getenv(env_name);
  if (raw == nullptr) {
    return;
  }
  AddIdentifierTokenAndVariants(out, raw);
}

void AddEnvironmentPathTailToken(std::vector<std::string>& out, const char* env_name) {
  if (env_name == nullptr) {
    return;
  }
  const char* raw = std::getenv(env_name);
  if (raw == nullptr) {
    return;
  }
  AddIdentifierTokenAndVariants(out, TailPathSegment(raw));
}

void AddSystemHostnameTokens(std::vector<std::string>& out) {
#if defined(_WIN32)
  char name[256] = {};
  DWORD size = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
  if (GetComputerNameA(name, &size) != 0 && size > 0U) {
    AddIdentifierTokenAndVariants(out, std::string(name, size));
  }
#elif defined(__linux__) || defined(__APPLE__)
  char name[256] = {};
  if (gethostname(name, sizeof(name)) == 0) {
    name[sizeof(name) - 1U] = '\0';
    AddIdentifierTokenAndVariants(out, std::string(name));
  }
#endif
}

std::optional<std::string> ParseMacMediaSpeedHint(std::string_view line) {
  const std::string trimmed = Trim(line);
  if (!StartsWith(trimmed, "media:")) {
    return std::nullopt;
  }

  const std::size_t open = trimmed.find('(');
  const std::size_t close = trimmed.find(')', open == std::string::npos ? 0U : open + 1U);
  if (open != std::string::npos && close != std::string::npos && close > open + 1U) {
    const std::string token = FirstToken(trimmed.substr(open + 1U, close - open - 1U));
    const std::string token_lower = ToLower(token);
    if (token_lower.find("base") != std::string::npos ||
        token_lower.find("mbps") != std::string::npos ||
        token_lower.find("gbps") != std::string::npos) {
      return NormalizeLinkSpeedHint(token);
    }
  }

  return NormalizeLinkSpeedHint(Trim(trimmed.substr(std::string_view("media:").size())));
}

std::optional<std::string> ParseLinuxEthtoolSpeedHint(const std::string& output) {
  std::istringstream in(output);
  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    if (!StartsWith(trimmed, "Speed:")) {
      continue;
    }

    const std::string value = Trim(trimmed.substr(std::string_view("Speed:").size()));
    const auto normalized = NormalizeLinkSpeedHint(value);
    if (normalized.has_value()) {
      return normalized;
    }
  }
  return std::nullopt;
}

NicInterfaceHighlight& GetOrCreateInterface(NicHighlights& highlights,
                                            const std::string& interface_name) {
  for (auto& iface : highlights.interfaces) {
    if (iface.name == interface_name) {
      return iface;
    }
  }

  highlights.interfaces.push_back(NicInterfaceHighlight{});
  highlights.interfaces.back().name = interface_name;
  return highlights.interfaces.back();
}

void MarkDefaultRoute(NicHighlights& highlights, const std::string& interface_name) {
  if (interface_name.empty()) {
    return;
  }

  highlights.default_route_interface = interface_name;
  NicInterfaceHighlight& iface = GetOrCreateInterface(highlights, interface_name);
  iface.has_default_route = true;
}

bool RunShellCommand(const std::string& command, std::string& output, int& exit_code,
                     std::string& error) {
  output.clear();
  exit_code = -1;
  error.clear();

#if defined(_WIN32)
  const std::string wrapped = command + " 2>&1";
  FILE* pipe = _popen(wrapped.c_str(), "r");
#else
  const std::string wrapped = command + " 2>&1";
  FILE* pipe = popen(wrapped.c_str(), "r");
#endif
  if (pipe == nullptr) {
    error = "failed to execute command: " + command;
    return false;
  }

  char buffer[4096];
  while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
    output.append(buffer);
  }

#if defined(_WIN32)
  const int raw_status = _pclose(pipe);
  exit_code = raw_status;
#else
  const int raw_status = pclose(pipe);
  if (raw_status == -1) {
    exit_code = -1;
  } else if (WIFEXITED(raw_status)) {
    exit_code = WEXITSTATUS(raw_status);
  } else {
    exit_code = raw_status;
  }
#endif

  return true;
}

bool OutputSuggestsMissingCommand(std::string_view output) {
  const std::string lower = ToLower(output);
  return lower.find("not found") != std::string::npos ||
         lower.find("not recognized") != std::string::npos ||
         lower.find("no such file") != std::string::npos;
}

bool IsCommandAvailable(const std::string& command_name) {
  std::string output;
  int exit_code = -1;
  std::string error;
#if defined(_WIN32)
  const std::string command = "where " + command_name;
#else
  const std::string command = "command -v " + command_name;
#endif
  if (!RunShellCommand(command, output, exit_code, error)) {
    return false;
  }
  return exit_code == 0;
}

NicCommandCapture CaptureCommand(const std::string& file_name, const std::string& command) {
  NicCommandCapture capture;
  capture.file_name = file_name;
  capture.command = command;

  std::string error;
  if (!RunShellCommand(command, capture.output, capture.exit_code, error)) {
    capture.command_available = false;
    capture.output = "probe command execution failed: " + error;
    capture.exit_code = -1;
    return capture;
  }

  if (capture.exit_code == 127 || OutputSuggestsMissingCommand(capture.output)) {
    capture.command_available = false;
  }
  return capture;
}

void ParseLinuxIpAddressOutput(const std::string& output, NicHighlights& highlights) {
  std::istringstream in(output);
  std::string line;
  std::string current_iface;

  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(trimmed[0])) != 0) {
      const std::size_t first_colon = trimmed.find(':');
      if (first_colon != std::string::npos) {
        std::size_t name_start = first_colon + 1U;
        while (name_start < trimmed.size() &&
               std::isspace(static_cast<unsigned char>(trimmed[name_start])) != 0) {
          ++name_start;
        }

        const std::size_t second_colon = trimmed.find(':', name_start);
        if (second_colon != std::string::npos) {
          current_iface =
              NormalizeInterfaceName(trimmed.substr(name_start, second_colon - name_start));
          if (!current_iface.empty()) {
            NicInterfaceHighlight& iface = GetOrCreateInterface(highlights, current_iface);
            if (const auto mtu = ExtractUnsignedAfterToken(trimmed, "mtu "); mtu.has_value()) {
              iface.mtu_hint = mtu.value();
            }
          }
          continue;
        }
      }
    }

    if (current_iface.empty()) {
      continue;
    }

    NicInterfaceHighlight& iface = GetOrCreateInterface(highlights, current_iface);
    if (StartsWith(trimmed, "link/ether ")) {
      iface.mac_address = FirstToken(trimmed.substr(std::string_view("link/ether ").size()));
      continue;
    }

    if (StartsWith(trimmed, "inet ")) {
      std::string ip = FirstToken(trimmed.substr(std::string_view("inet ").size()));
      ip = StripCidrSuffix(ip);
      AddUnique(iface.ipv4_addresses, ip);
      continue;
    }

    if (StartsWith(trimmed, "inet6 ")) {
      std::string ip = FirstToken(trimmed.substr(std::string_view("inet6 ").size()));
      ip = StripIpv6Zone(StripCidrSuffix(ip));
      AddUnique(iface.ipv6_addresses, ip);
      continue;
    }
  }
}

void ParseLinuxRouteOutput(const std::string& output, NicHighlights& highlights) {
  std::istringstream in(output);
  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    if (!StartsWith(trimmed, "default")) {
      continue;
    }

    const std::vector<std::string> tokens = SplitWhitespace(trimmed);
    for (std::size_t i = 0; i + 1U < tokens.size(); ++i) {
      if (tokens[i] == "dev") {
        MarkDefaultRoute(highlights, NormalizeInterfaceName(tokens[i + 1U]));
        return;
      }
    }
  }
}

void ParseMacIfconfigOutput(const std::string& output, NicHighlights& highlights) {
  std::istringstream in(output);
  std::string line;
  std::string current_iface;

  while (std::getline(in, line)) {
    if (!line.empty() && std::isspace(static_cast<unsigned char>(line[0])) == 0) {
      const std::size_t colon = line.find(':');
      if (colon != std::string::npos) {
        current_iface = NormalizeInterfaceName(line.substr(0, colon));
        if (!current_iface.empty()) {
          NicInterfaceHighlight& iface = GetOrCreateInterface(highlights, current_iface);
          if (const auto mtu = ExtractUnsignedAfterToken(line, "mtu "); mtu.has_value()) {
            iface.mtu_hint = mtu.value();
          }
        }
      }
      continue;
    }

    if (current_iface.empty()) {
      continue;
    }

    const std::string trimmed = Trim(line);
    NicInterfaceHighlight& iface = GetOrCreateInterface(highlights, current_iface);

    if (StartsWith(trimmed, "ether ")) {
      iface.mac_address = FirstToken(trimmed.substr(std::string_view("ether ").size()));
      continue;
    }

    if (StartsWith(trimmed, "inet ")) {
      const std::string ip = FirstToken(trimmed.substr(std::string_view("inet ").size()));
      AddUnique(iface.ipv4_addresses, ip);
      continue;
    }

    if (StartsWith(trimmed, "inet6 ")) {
      std::string ip = FirstToken(trimmed.substr(std::string_view("inet6 ").size()));
      ip = StripIpv6Zone(ip);
      AddUnique(iface.ipv6_addresses, ip);
      continue;
    }

    if (StartsWith(trimmed, "media:")) {
      if (const auto speed = ParseMacMediaSpeedHint(trimmed); speed.has_value()) {
        iface.link_speed_hint = speed.value();
      }
      continue;
    }
  }
}

void ParseMacRouteGetDefaultOutput(const std::string& output, NicHighlights& highlights) {
  std::istringstream in(output);
  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    if (!StartsWith(trimmed, "interface:")) {
      continue;
    }

    const std::string value = Trim(trimmed.substr(std::string_view("interface:").size()));
    MarkDefaultRoute(highlights, NormalizeInterfaceName(value));
    return;
  }
}

void ParseMacNetstatRouteOutput(const std::string& output, NicHighlights& highlights) {
  std::istringstream in(output);
  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
      continue;
    }

    const std::vector<std::string> tokens = SplitWhitespace(trimmed);
    if (tokens.size() < 2U) {
      continue;
    }

    if (tokens[0] == "default") {
      MarkDefaultRoute(highlights, NormalizeInterfaceName(tokens.back()));
      return;
    }
  }
}

std::string NormalizeWindowsAddressToken(std::string value) {
  value = Trim(value);
  const std::size_t paren = value.find('(');
  if (paren != std::string::npos) {
    value = Trim(value.substr(0, paren));
  }
  return FirstToken(value);
}

void ParseWindowsIpconfigOutput(const std::string& output, NicHighlights& highlights) {
  std::istringstream in(output);
  std::string line;
  std::string current_iface;

  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
      continue;
    }

    const std::string lower = ToLower(trimmed);
    const std::size_t adapter_pos = lower.find("adapter ");
    if (adapter_pos != std::string::npos && !trimmed.empty() && trimmed.back() == ':') {
      const std::size_t name_start = adapter_pos + std::string("adapter ").size();
      std::string iface_name = trimmed.substr(name_start, trimmed.size() - name_start - 1U);
      iface_name = Trim(iface_name);
      current_iface = NormalizeInterfaceName(iface_name);
      if (!current_iface.empty()) {
        (void)GetOrCreateInterface(highlights, current_iface);
      }
      continue;
    }

    if (current_iface.empty()) {
      continue;
    }

    NicInterfaceHighlight& iface = GetOrCreateInterface(highlights, current_iface);

    if (lower.find("physical address") != std::string::npos) {
      iface.mac_address = StripPrefixUntil(trimmed, ':');
      continue;
    }

    if (lower.find("ipv4 address") != std::string::npos) {
      const std::string ip = NormalizeWindowsAddressToken(StripPrefixUntil(trimmed, ':'));
      AddUnique(iface.ipv4_addresses, ip);
      continue;
    }

    if (lower.find("ipv6 address") != std::string::npos ||
        lower.find("link-local ipv6 address") != std::string::npos ||
        lower.find("temporary ipv6 address") != std::string::npos) {
      std::string ip = NormalizeWindowsAddressToken(StripPrefixUntil(trimmed, ':'));
      ip = StripIpv6Zone(ip);
      AddUnique(iface.ipv6_addresses, ip);
      continue;
    }

    if (lower.find("default gateway") != std::string::npos) {
      const std::string gateway = NormalizeWindowsAddressToken(StripPrefixUntil(trimmed, ':'));
      if (!gateway.empty()) {
        MarkDefaultRoute(highlights, current_iface);
      }
      continue;
    }

    if (lower.find("mtu") != std::string::npos) {
      std::string value = StripPrefixUntil(trimmed, ':');
      if (value.empty()) {
        value = trimmed;
      }
      if (const auto mtu = ParseFirstUnsigned(value); mtu.has_value()) {
        iface.mtu_hint = mtu.value();
      }
      continue;
    }

    if (lower.find("link speed") != std::string::npos) {
      std::string value = StripPrefixUntil(trimmed, ':');
      if (value.empty()) {
        value = trimmed;
      }
      if (const auto speed = NormalizeLinkSpeedHint(value); speed.has_value()) {
        iface.link_speed_hint = speed.value();
      }
      continue;
    }
  }
}

void SortInterfaces(NicHighlights& highlights) {
  std::sort(highlights.interfaces.begin(), highlights.interfaces.end(),
            [](const NicInterfaceHighlight& lhs, const NicInterfaceHighlight& rhs) {
              return lhs.name < rhs.name;
            });
}

void CollectLinuxNicProbe(NicProbeSnapshot& snapshot) {
  NicCommandCapture ip_a = CaptureCommand("nic_ip_a.txt", "ip a");
  ParseLinuxIpAddressOutput(ip_a.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ip_a));

  NicCommandCapture ip_r = CaptureCommand("nic_ip_r.txt", "ip r");
  ParseLinuxRouteOutput(ip_r.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ip_r));

  NicCommandCapture ethtool_capture;
  ethtool_capture.file_name = "nic_ethtool.txt";
  ethtool_capture.command = "ethtool <interface>";

  if (!IsCommandAvailable("ethtool")) {
    ethtool_capture.command_available = false;
    ethtool_capture.exit_code = 127;
    ethtool_capture.output = "ethtool not available on host PATH.\n";
    snapshot.raw_captures.push_back(std::move(ethtool_capture));
    SortInterfaces(snapshot.highlights);
    return;
  }

  std::vector<std::string> interface_names;
  interface_names.reserve(snapshot.highlights.interfaces.size());
  for (const auto& iface : snapshot.highlights.interfaces) {
    if (iface.name.empty() || iface.name == "lo") {
      continue;
    }
    interface_names.push_back(iface.name);
  }

  if (interface_names.empty()) {
    interface_names.push_back("eth0");
  }

  int aggregate_exit_code = 0;
  std::ostringstream aggregate;
  for (const auto& iface_name : interface_names) {
    const std::string command = "ethtool " + iface_name;
    NicCommandCapture per_iface = CaptureCommand("", command);
    NicInterfaceHighlight& iface = GetOrCreateInterface(snapshot.highlights, iface_name);
    if (const auto speed = ParseLinuxEthtoolSpeedHint(per_iface.output); speed.has_value()) {
      iface.link_speed_hint = speed.value();
    }
    if (per_iface.exit_code != 0) {
      aggregate_exit_code = per_iface.exit_code;
    }

    aggregate << "# command: " << command << "\n";
    aggregate << "# exit_code: " << per_iface.exit_code << "\n\n";
    aggregate << per_iface.output;
    if (!per_iface.output.empty() && per_iface.output.back() != '\n') {
      aggregate << '\n';
    }
    aggregate << "\n";
  }

  ethtool_capture.exit_code = aggregate_exit_code;
  ethtool_capture.command_available = true;
  ethtool_capture.output = aggregate.str();
  snapshot.raw_captures.push_back(std::move(ethtool_capture));
  SortInterfaces(snapshot.highlights);
}

void CollectMacNicProbe(NicProbeSnapshot& snapshot) {
  NicCommandCapture ifconfig_a = CaptureCommand("nic_ifconfig_a.txt", "ifconfig -a");
  ParseMacIfconfigOutput(ifconfig_a.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ifconfig_a));

  NicCommandCapture netstat_rn = CaptureCommand("nic_netstat_rn.txt", "netstat -rn");
  ParseMacNetstatRouteOutput(netstat_rn.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(netstat_rn));

  NicCommandCapture route_default =
      CaptureCommand("nic_route_get_default.txt", "route -n get default");
  ParseMacRouteGetDefaultOutput(route_default.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(route_default));

  SortInterfaces(snapshot.highlights);
}

void CollectWindowsNicProbe(NicProbeSnapshot& snapshot) {
  NicCommandCapture ipconfig_all = CaptureCommand("nic_ipconfig_all.txt", "ipconfig /all");
  ParseWindowsIpconfigOutput(ipconfig_all.output, snapshot.highlights);
  snapshot.raw_captures.push_back(std::move(ipconfig_all));
  SortInterfaces(snapshot.highlights);
}

void CollectUnsupportedPlatformNicProbe(NicProbeSnapshot& snapshot) {
  NicCommandCapture unsupported;
  unsupported.file_name = "nic_probe_unavailable.txt";
  unsupported.command = "unsupported_platform";
  unsupported.exit_code = 127;
  unsupported.command_available = false;
  unsupported.output = "Network probe is not implemented for this platform.\n";
  snapshot.raw_captures.push_back(std::move(unsupported));
}

std::string DetectOsName() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string DetectOsVersion() {
#if defined(__linux__) || defined(__APPLE__)
  struct utsname uts{};
  if (uname(&uts) == 0) {
    return std::string(uts.release);
  }
#endif
  return "unknown";
}

std::string ProbeCpuModel() {
#if defined(__linux__)
  std::ifstream input("/proc/cpuinfo");
  std::string line;
  constexpr std::string_view kPrefix = "model name";
  while (std::getline(input, line)) {
    if (line.rfind(kPrefix, 0) == 0U) {
      const std::size_t colon = line.find(':');
      if (colon == std::string::npos || colon + 1U >= line.size()) {
        continue;
      }
      std::string value = line.substr(colon + 1U);
      const std::size_t first = value.find_first_not_of(" \t");
      if (first == std::string::npos) {
        continue;
      }
      const std::size_t last = value.find_last_not_of(" \t");
      return value.substr(first, last - first + 1U);
    }
  }
#elif defined(__APPLE__)
  char buffer[256] = {};
  std::size_t length = sizeof(buffer);
  if (sysctlbyname("machdep.cpu.brand_string", buffer, &length, nullptr, 0) == 0 && length > 0U) {
    return std::string(buffer);
  }
#endif
  return "unknown";
}

std::uint64_t ProbeRamTotalBytes() {
#if defined(__linux__)
  struct sysinfo info{};
  if (sysinfo(&info) == 0) {
    return static_cast<std::uint64_t>(info.totalram) * static_cast<std::uint64_t>(info.mem_unit);
  }
#elif defined(__APPLE__)
  std::uint64_t value = 0;
  std::size_t length = sizeof(value);
  if (sysctlbyname("hw.memsize", &value, &length, nullptr, 0) == 0) {
    return value;
  }
#elif defined(_WIN32)
  MEMORYSTATUSEX memory_status{};
  memory_status.dwLength = sizeof(memory_status);
  if (GlobalMemoryStatusEx(&memory_status) != 0) {
    return static_cast<std::uint64_t>(memory_status.ullTotalPhys);
  }
#endif
  return 0;
}

std::uint64_t ProbeUptimeSeconds() {
#if defined(__linux__)
  struct sysinfo info{};
  if (sysinfo(&info) == 0 && info.uptime >= 0) {
    return static_cast<std::uint64_t>(info.uptime);
  }
#elif defined(__APPLE__)
  // Boot time via sysctl keeps this independent of sleep/wake counters.
  struct timeval boot_time{};
  std::size_t length = sizeof(boot_time);
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};
  if (sysctl(mib, 2, &boot_time, &length, nullptr, 0) == 0 && boot_time.tv_sec > 0) {
    const auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    if (now_seconds > static_cast<std::int64_t>(boot_time.tv_sec)) {
      return static_cast<std::uint64_t>(now_seconds - static_cast<std::int64_t>(boot_time.tv_sec));
    }
  }

  // Fallback for environments where kern.boottime is unavailable.
  struct timespec uptime_spec{};
  if (clock_gettime(CLOCK_UPTIME_RAW, &uptime_spec) == 0 && uptime_spec.tv_sec >= 0) {
    return static_cast<std::uint64_t>(uptime_spec.tv_sec);
  }
#elif defined(_WIN32)
  return static_cast<std::uint64_t>(GetTickCount64() / 1000ULL);
#endif
  return 0;
}

std::array<std::optional<double>, 3> ProbeLoadAverages() {
  std::array<std::optional<double>, 3> values;
#if defined(__linux__) || defined(__APPLE__)
  double loads[3] = {0.0, 0.0, 0.0};
  if (getloadavg(loads, 3) == 3) {
    values[0] = loads[0];
    values[1] = loads[1];
    values[2] = loads[2];
  }
#endif
  return values;
}

void WriteJsonStringArray(std::ostringstream& out, const std::vector<std::string>& values) {
  out << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) {
      out << ",";
    }
    out << "\"" << EscapeJson(values[i]) << "\"";
  }
  out << "]";
}

void WriteNicHighlightsJson(std::ostringstream& out, const NicHighlights& highlights) {
  out << "\"nic_highlights\":{";
  out << "\"default_route_interface\":";
  if (highlights.default_route_interface.has_value()) {
    out << "\"" << EscapeJson(highlights.default_route_interface.value()) << "\"";
  } else {
    out << "null";
  }
  out << ",\"interfaces\":[";

  for (std::size_t i = 0; i < highlights.interfaces.size(); ++i) {
    const auto& iface = highlights.interfaces[i];
    if (i != 0U) {
      out << ",";
    }

    out << "{";
    out << "\"name\":\"" << EscapeJson(iface.name) << "\",";
    out << "\"mac_address\":";
    if (iface.mac_address.has_value()) {
      out << "\"" << EscapeJson(iface.mac_address.value()) << "\"";
    } else {
      out << "null";
    }
    out << ",\"ipv4_addresses\":";
    WriteJsonStringArray(out, iface.ipv4_addresses);
    out << ",\"ipv6_addresses\":";
    WriteJsonStringArray(out, iface.ipv6_addresses);
    out << ",\"mtu_hint\":";
    if (iface.mtu_hint.has_value()) {
      out << iface.mtu_hint.value();
    } else {
      out << "null";
    }
    out << ",\"link_speed_hint\":";
    if (iface.link_speed_hint.has_value()) {
      out << "\"" << EscapeJson(iface.link_speed_hint.value()) << "\"";
    } else {
      out << "null";
    }
    out << ",\"has_default_route\":" << (iface.has_default_route ? "true" : "false");
    out << "}";
  }

  out << "]}";
}

} // namespace

bool CollectHostProbeSnapshot(HostProbeSnapshot& snapshot, std::string& error) {
  error.clear();
  snapshot = HostProbeSnapshot{};

  snapshot.captured_at = std::chrono::system_clock::now();
  snapshot.os_name = DetectOsName();
  snapshot.os_version = DetectOsVersion();
  snapshot.cpu_model = ProbeCpuModel();
  snapshot.cpu_logical_cores = std::thread::hardware_concurrency();
  snapshot.ram_total_bytes = ProbeRamTotalBytes();
  snapshot.uptime_seconds = ProbeUptimeSeconds();

  const auto load_averages = ProbeLoadAverages();
  snapshot.load_avg_1m = load_averages[0];
  snapshot.load_avg_5m = load_averages[1];
  snapshot.load_avg_15m = load_averages[2];
  return true;
}

bool CollectNicProbeSnapshot(NicProbeSnapshot& snapshot, std::string& error) {
  error.clear();
  snapshot = NicProbeSnapshot{};

#if defined(_WIN32)
  CollectWindowsNicProbe(snapshot);
#elif defined(__linux__)
  CollectLinuxNicProbe(snapshot);
#elif defined(__APPLE__)
  CollectMacNicProbe(snapshot);
#else
  CollectUnsupportedPlatformNicProbe(snapshot);
#endif

  if (snapshot.raw_captures.empty()) {
    CollectUnsupportedPlatformNicProbe(snapshot);
  }

  return true;
}

void BuildIdentifierRedactionContext(IdentifierRedactionContext& context) {
  context = IdentifierRedactionContext{};

  // Environment variables make redaction deterministic in CI and on local
  // hosts where the same identifiers show up in multiple command outputs.
  AddEnvironmentToken(context.hostname_tokens, "HOSTNAME");
  AddEnvironmentToken(context.hostname_tokens, "COMPUTERNAME");
  AddSystemHostnameTokens(context.hostname_tokens);

  AddEnvironmentToken(context.username_tokens, "USER");
  AddEnvironmentToken(context.username_tokens, "USERNAME");
  AddEnvironmentToken(context.username_tokens, "LOGNAME");
  AddEnvironmentToken(context.username_tokens, "SUDO_USER");

  AddEnvironmentPathTailToken(context.username_tokens, "HOME");
  AddEnvironmentPathTailToken(context.username_tokens, "USERPROFILE");
}

void RedactHostProbeSnapshot(HostProbeSnapshot& snapshot,
                             const IdentifierRedactionContext& context) {
  RedactStringValue(snapshot.os_name, context);
  RedactStringValue(snapshot.os_version, context);
  RedactStringValue(snapshot.cpu_model, context);
  RedactNicHighlights(snapshot.nic_highlights, context);
}

void RedactNicProbeSnapshot(NicProbeSnapshot& snapshot, const IdentifierRedactionContext& context) {
  RedactNicHighlights(snapshot.highlights, context);
  for (auto& capture : snapshot.raw_captures) {
    RedactStringValue(capture.file_name, context);
    RedactStringValue(capture.command, context);
    RedactStringValue(capture.output, context);
  }
}

std::string ToJson(const HostProbeSnapshot& snapshot) {
  std::ostringstream out;
  out << "{"
      << "\"captured_at_utc\":\"" << FormatUtcTimestamp(snapshot.captured_at) << "\","
      << "\"os\":{"
      << "\"name\":\"" << EscapeJson(snapshot.os_name) << "\","
      << "\"version\":\"" << EscapeJson(snapshot.os_version) << "\""
      << "},"
      << "\"cpu\":{"
      << "\"model\":\"" << EscapeJson(snapshot.cpu_model) << "\","
      << "\"logical_cores\":" << snapshot.cpu_logical_cores << "},"
      << "\"ram_total_bytes\":" << snapshot.ram_total_bytes << ","
      << "\"uptime_seconds\":" << snapshot.uptime_seconds << ","
      << "\"load_avg\":{"
      << "\"one_min\":";
  if (snapshot.load_avg_1m.has_value()) {
    out << FormatDouble(snapshot.load_avg_1m.value());
  } else {
    out << "null";
  }
  out << ",\"five_min\":";
  if (snapshot.load_avg_5m.has_value()) {
    out << FormatDouble(snapshot.load_avg_5m.value());
  } else {
    out << "null";
  }
  out << ",\"fifteen_min\":";
  if (snapshot.load_avg_15m.has_value()) {
    out << FormatDouble(snapshot.load_avg_15m.value());
  } else {
    out << "null";
  }

  out << "},";
  WriteNicHighlightsJson(out, snapshot.nic_highlights);
  out << "}";
  return out.str();
}

} // namespace labops::hostprobe
