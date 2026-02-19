#pragma once

#include "core/time_utils.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace labops::core::logging {

enum class LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

struct LogFieldView {
  std::string_view key;
  std::string_view value;
};

inline const char* ToString(LogLevel level) {
  switch (level) {
  case LogLevel::kDebug:
    return "DEBUG";
  case LogLevel::kInfo:
    return "INFO";
  case LogLevel::kWarn:
    return "WARN";
  case LogLevel::kError:
    return "ERROR";
  }

  return "INFO";
}

inline std::string ExpectedLogLevelList() {
  return "debug|info|warn|error";
}

inline bool ParseLogLevel(std::string_view raw, LogLevel& level, std::string& error) {
  error.clear();

  if (raw.empty()) {
    error = "missing value for --log-level (expected " + ExpectedLogLevelList() + ")";
    return false;
  }

  std::string normalized(raw);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (normalized == "debug") {
    level = LogLevel::kDebug;
    return true;
  }
  if (normalized == "info") {
    level = LogLevel::kInfo;
    return true;
  }
  if (normalized == "warn" || normalized == "warning") {
    level = LogLevel::kWarn;
    return true;
  }
  if (normalized == "error") {
    level = LogLevel::kError;
    return true;
  }

  error =
      "invalid --log-level '" + std::string(raw) + "' (expected " + ExpectedLogLevelList() + ")";
  return false;
}

class Logger {
public:
  explicit Logger(LogLevel min_level = LogLevel::kInfo, std::ostream& out = std::cerr)
      : min_level_(min_level), out_(&out) {}

  void SetMinLevel(LogLevel level) {
    min_level_ = level;
  }

  LogLevel MinLevel() const {
    return min_level_;
  }

  void SetRunId(std::string run_id) {
    run_id_ = std::move(run_id);
  }

  const std::string& RunId() const {
    return run_id_;
  }

  bool ShouldLog(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(min_level_);
  }

  void Log(LogLevel level, std::string_view message,
           std::initializer_list<LogFieldView> fields = {}) {
    if (!ShouldLog(level)) {
      return;
    }

    (*out_) << "ts_utc=" << core::FormatUtcTimestamp(std::chrono::system_clock::now())
            << " level=" << ToString(level) << " run_id=" << Quote(run_id_)
            << " msg=" << Quote(message);

    for (const auto& field : fields) {
      (*out_) << ' ' << field.key << '=' << Quote(field.value);
    }

    (*out_) << '\n';
    out_->flush();
  }

  void Debug(std::string_view message, std::initializer_list<LogFieldView> fields = {}) {
    Log(LogLevel::kDebug, message, fields);
  }

  void Info(std::string_view message, std::initializer_list<LogFieldView> fields = {}) {
    Log(LogLevel::kInfo, message, fields);
  }

  void Warn(std::string_view message, std::initializer_list<LogFieldView> fields = {}) {
    Log(LogLevel::kWarn, message, fields);
  }

  void Error(std::string_view message, std::initializer_list<LogFieldView> fields = {}) {
    Log(LogLevel::kError, message, fields);
  }

private:
  static std::string EscapeForQuoted(std::string_view raw) {
    std::string escaped;
    escaped.reserve(raw.size());

    for (const char c : raw) {
      switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(c);
        break;
      }
    }

    return escaped;
  }

  static std::string Quote(std::string_view raw) {
    return std::string("\"") + EscapeForQuoted(raw) + "\"";
  }

  LogLevel min_level_ = LogLevel::kInfo;
  std::ostream* out_ = &std::cerr;
  std::string run_id_ = "-";
};

} // namespace labops::core::logging
