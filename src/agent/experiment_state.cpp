#include "agent/experiment_state.hpp"

#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace labops::agent {

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

std::string FormatJsonDouble(double value) {
  if (!std::isfinite(value)) {
    return "0.0";
  }

  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << value;
  return out.str();
}

const char* HypothesisStatusToString(HypothesisStatus status) {
  switch (status) {
  case HypothesisStatus::kOpen:
    return "open";
  case HypothesisStatus::kSupported:
    return "supported";
  case HypothesisStatus::kRejected:
    return "rejected";
  case HypothesisStatus::kNeedsHuman:
    return "needs_human";
  }
  return "open";
}

const char* ResultStatusToString(ResultStatus status) {
  switch (status) {
  case ResultStatus::kPass:
    return "pass";
  case ResultStatus::kFail:
    return "fail";
  case ResultStatus::kInconclusive:
    return "inconclusive";
  }
  return "inconclusive";
}

std::string ToJson(const Hypothesis& hypothesis) {
  std::ostringstream out;
  out << "{"
      << "\"id\":\"" << EscapeJson(hypothesis.id) << "\","
      << "\"statement\":\"" << EscapeJson(hypothesis.statement) << "\","
      << "\"variable_name\":\"" << EscapeJson(hypothesis.variable_name) << "\","
      << "\"status\":\"" << HypothesisStatusToString(hypothesis.status) << "\""
      << "}";
  return out.str();
}

std::string ToJson(const TestedVariable& variable) {
  std::ostringstream out;
  out << "{"
      << "\"name\":\"" << EscapeJson(variable.name) << "\","
      << "\"baseline_value\":\"" << EscapeJson(variable.baseline_value) << "\","
      << "\"candidate_value\":\"" << EscapeJson(variable.candidate_value) << "\""
      << "}";
  return out.str();
}

std::string ToJson(const ResultRow& row) {
  std::ostringstream out;
  out << "{"
      << "\"experiment_id\":\"" << EscapeJson(row.experiment_id) << "\","
      << "\"hypothesis_id\":\"" << EscapeJson(row.hypothesis_id) << "\","
      << "\"variable_name\":\"" << EscapeJson(row.variable_name) << "\","
      << "\"variable_value\":\"" << EscapeJson(row.variable_value) << "\","
      << "\"result\":\"" << ResultStatusToString(row.result) << "\","
      << "\"evidence_run_id\":\"" << EscapeJson(row.evidence_run_id) << "\","
      << "\"avg_fps\":" << FormatJsonDouble(row.avg_fps) << ","
      << "\"drop_rate_percent\":" << FormatJsonDouble(row.drop_rate_percent) << ","
      << "\"jitter_p95_us\":" << FormatJsonDouble(row.jitter_p95_us) << ","
      << "\"notes\":\"" << EscapeJson(row.notes) << "\""
      << "}";
  return out.str();
}

template <typename T>
std::string SerializeArray(const std::vector<T>& rows, std::string (*to_json)(const T&)) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << to_json(rows[i]);
  }
  out << "]";
  return out.str();
}

} // namespace

std::string ToJson(const ExperimentState& state) {
  std::ostringstream out;
  out << "{"
      << "\"session_id\":\"" << EscapeJson(state.session_id) << "\","
      << "\"scenario_id\":\"" << EscapeJson(state.scenario_id) << "\","
      << "\"baseline_id\":\"" << EscapeJson(state.baseline_id) << "\","
      << "\"seed\":" << state.seed << ","
      << "\"created_at_utc\":\"" << FormatUtcTimestamp(state.created_at) << "\","
      << "\"updated_at_utc\":\"" << FormatUtcTimestamp(state.updated_at) << "\","
      << "\"next_action\":\"" << EscapeJson(state.next_action) << "\","
      << "\"hypotheses\":" << SerializeArray<Hypothesis>(state.hypotheses, &ToJson) << ","
      << "\"tested_variables\":"
      << SerializeArray<TestedVariable>(state.tested_variables, &ToJson) << ","
      << "\"results_table\":" << SerializeArray<ResultRow>(state.results_table, &ToJson)
      << "}";
  return out.str();
}

} // namespace labops::agent
