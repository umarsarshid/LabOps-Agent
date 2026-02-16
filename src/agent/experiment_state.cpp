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

void WriteFieldDelimiter(std::ostringstream& out, bool& first_field) {
  if (!first_field) {
    out << ",";
  }
  first_field = false;
}

void WriteJsonStringField(std::ostringstream& out, std::string_view key, std::string_view value,
                          bool& first_field) {
  WriteFieldDelimiter(out, first_field);
  out << "\"" << key << "\":\"" << EscapeJson(value) << "\"";
}

void WriteJsonRawField(std::ostringstream& out, std::string_view key, std::string_view raw_value,
                       bool& first_field) {
  WriteFieldDelimiter(out, first_field);
  out << "\"" << key << "\":" << raw_value;
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
  bool first_field = true;
  out << "{";
  WriteJsonStringField(out, "id", hypothesis.id, first_field);
  WriteJsonStringField(out, "statement", hypothesis.statement, first_field);
  WriteJsonStringField(out, "variable_name", hypothesis.variable_name, first_field);
  WriteJsonStringField(out, "status", HypothesisStatusToString(hypothesis.status), first_field);
  out << "}";
  return out.str();
}

std::string ToJson(const TestedVariable& variable) {
  std::ostringstream out;
  bool first_field = true;
  out << "{";
  WriteJsonStringField(out, "name", variable.name, first_field);
  WriteJsonStringField(out, "baseline_value", variable.baseline_value, first_field);
  WriteJsonStringField(out, "candidate_value", variable.candidate_value, first_field);
  out << "}";
  return out.str();
}

std::string ToJson(const ResultRow& row) {
  std::ostringstream out;
  bool first_field = true;
  out << "{";
  WriteJsonStringField(out, "experiment_id", row.experiment_id, first_field);
  WriteJsonStringField(out, "hypothesis_id", row.hypothesis_id, first_field);
  WriteJsonStringField(out, "variable_name", row.variable_name, first_field);
  WriteJsonStringField(out, "variable_value", row.variable_value, first_field);
  WriteJsonStringField(out, "result", ResultStatusToString(row.result), first_field);
  WriteJsonStringField(out, "evidence_run_id", row.evidence_run_id, first_field);
  WriteJsonRawField(out, "avg_fps", FormatJsonDouble(row.avg_fps), first_field);
  WriteJsonRawField(out, "drop_rate_percent", FormatJsonDouble(row.drop_rate_percent), first_field);
  WriteJsonRawField(out, "jitter_p95_us", FormatJsonDouble(row.jitter_p95_us), first_field);
  WriteJsonStringField(out, "notes", row.notes, first_field);
  out << "}";
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
  bool first_field = true;
  out << "{";
  WriteJsonStringField(out, "session_id", state.session_id, first_field);
  WriteJsonStringField(out, "scenario_id", state.scenario_id, first_field);
  WriteJsonStringField(out, "baseline_id", state.baseline_id, first_field);
  WriteJsonRawField(out, "seed", std::to_string(state.seed), first_field);
  WriteJsonStringField(out, "created_at_utc", FormatUtcTimestamp(state.created_at), first_field);
  WriteJsonStringField(out, "updated_at_utc", FormatUtcTimestamp(state.updated_at), first_field);
  WriteJsonStringField(out, "next_action", state.next_action, first_field);
  WriteJsonRawField(out, "hypotheses", SerializeArray<Hypothesis>(state.hypotheses, &ToJson),
                    first_field);
  WriteJsonRawField(out, "tested_variables",
                    SerializeArray<TestedVariable>(state.tested_variables, &ToJson), first_field);
  WriteJsonRawField(out, "results_table", SerializeArray<ResultRow>(state.results_table, &ToJson),
                    first_field);
  out << "}";
  return out.str();
}

} // namespace labops::agent
