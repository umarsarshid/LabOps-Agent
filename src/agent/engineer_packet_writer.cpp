#include "agent/engineer_packet_writer.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace labops::agent {

namespace {

struct HypothesisRank {
  const Hypothesis* hypothesis = nullptr;
  int score = 0;
  std::size_t support_count = 0;
  std::size_t contradiction_count = 0;
  std::vector<const ResultRow*> rows;
};

const char* ToString(ResultStatus status) {
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

const char* ToString(HypothesisStatus status) {
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

bool EnsureOutputDir(const fs::path& output_dir, std::string& error) {
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }

  return true;
}

bool ValidateInput(const EngineerPacketInput& input, std::string& error) {
  if (input.state == nullptr) {
    error = "engineer packet input state cannot be null";
    return false;
  }
  if (input.baseline_scenario_path.empty()) {
    error = "baseline_scenario_path cannot be empty";
    return false;
  }
  if (input.baseline_bundle_dir.empty()) {
    error = "baseline_bundle_dir cannot be empty";
    return false;
  }
  if (input.configs_tried.empty()) {
    error = "configs_tried cannot be empty";
    return false;
  }
  return true;
}

std::map<std::string, PacketRunEvidence>
BuildEvidenceMap(const std::vector<PacketRunEvidence>& run_evidence) {
  std::map<std::string, PacketRunEvidence> evidence;
  for (const auto& item : run_evidence) {
    if (item.run_id.empty()) {
      continue;
    }
    evidence[item.run_id] = item;
  }
  return evidence;
}

void WriteEvidenceLinks(std::ofstream& out, const PacketRunEvidence& evidence) {
  out << "  - bundle: `" << evidence.bundle_dir.string() << "`\n";
  out << "  - run_json: `" << evidence.run_json_path.string() << "`\n";
  out << "  - events_jsonl: `" << evidence.events_jsonl_path.string() << "`\n";
  out << "  - metrics_json: `" << evidence.metrics_json_path.string() << "`\n";
  out << "  - summary_md: `" << evidence.summary_markdown_path.string() << "`\n";
  if (!evidence.diff_json_path.empty()) {
    out << "  - diff_json: `" << evidence.diff_json_path.string() << "`\n";
  }
  if (!evidence.diff_markdown_path.empty()) {
    out << "  - diff_md: `" << evidence.diff_markdown_path.string() << "`\n";
  }
}

std::vector<PacketConfigAttempt>
SortedAttempts(const std::vector<PacketConfigAttempt>& configs_tried) {
  std::vector<PacketConfigAttempt> attempts = configs_tried;
  std::sort(attempts.begin(), attempts.end(),
            [](const PacketConfigAttempt& a, const PacketConfigAttempt& b) {
              if (a.sequence != b.sequence) {
                return a.sequence < b.sequence;
              }
              return a.run_id < b.run_id;
            });
  return attempts;
}

std::vector<HypothesisRank> RankHypotheses(const ExperimentState& state) {
  std::map<std::string, HypothesisRank> ranks;

  for (const auto& hypothesis : state.hypotheses) {
    HypothesisRank rank;
    rank.hypothesis = &hypothesis;
    ranks[hypothesis.id] = rank;
  }

  for (const auto& row : state.results_table) {
    auto it = ranks.find(row.hypothesis_id);
    if (it == ranks.end()) {
      continue;
    }

    HypothesisRank& rank = it->second;
    rank.rows.push_back(&row);

    if (row.result == ResultStatus::kFail) {
      rank.score += 2;
      ++rank.support_count;
    } else if (row.result == ResultStatus::kPass) {
      rank.score -= 2;
      ++rank.contradiction_count;
    }
  }

  std::vector<HypothesisRank> ordered;
  ordered.reserve(ranks.size());
  for (auto& [_, rank] : ranks) {
    ordered.push_back(std::move(rank));
  }

  std::sort(ordered.begin(), ordered.end(), [](const HypothesisRank& a, const HypothesisRank& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    if (a.support_count != b.support_count) {
      return a.support_count > b.support_count;
    }
    if (a.contradiction_count != b.contradiction_count) {
      return a.contradiction_count < b.contradiction_count;
    }
    return a.hypothesis->id < b.hypothesis->id;
  });

  return ordered;
}

void WriteReproSteps(std::ofstream& out, const EngineerPacketInput& input,
                     const std::vector<PacketConfigAttempt>& attempts,
                     const std::map<std::string, PacketRunEvidence>& evidence) {
  out << "## Repro Steps\n\n";
  out << "1. Validate baseline scenario: `labops validate " << input.baseline_scenario_path.string()
      << "`\n";
  out << "2. Run baseline scenario and capture bundle under `" << input.baseline_bundle_dir.string()
      << "`.\n";

  std::size_t step = 3;
  for (const auto& attempt : attempts) {
    out << step << ". Apply knob `" << attempt.knob_name << "` (" << attempt.knob_path << ": `"
        << attempt.before_value << "` -> `" << attempt.after_value << "`) and run scenario `"
        << attempt.scenario_path.string() << "`.\n";

    const auto evidence_it = evidence.find(attempt.run_id);
    if (evidence_it != evidence.end()) {
      out << "   Evidence bundle: `" << evidence_it->second.bundle_dir.string() << "`\n";
      if (!evidence_it->second.diff_markdown_path.empty()) {
        out << "   Diff: `" << evidence_it->second.diff_markdown_path.string() << "`\n";
      }
    }

    ++step;
  }
  out << '\n';
}

void WriteConfigsTried(std::ofstream& out, const std::vector<PacketConfigAttempt>& attempts,
                       const std::map<std::string, PacketRunEvidence>& evidence) {
  out << "## Configs Tried\n\n";
  out << "| seq | run_id | knob | from | to | result | scenario_path | diff_md |\n";
  out << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
  for (const auto& attempt : attempts) {
    std::string diff_md = "";
    const auto evidence_it = evidence.find(attempt.run_id);
    if (evidence_it != evidence.end()) {
      diff_md = evidence_it->second.diff_markdown_path.string();
    }

    out << "| " << attempt.sequence << " | `" << attempt.run_id << "` | `" << attempt.knob_name
        << "` | `" << attempt.before_value << "` | `" << attempt.after_value << "` | `"
        << ToString(attempt.result) << "` | `" << attempt.scenario_path.string() << "` | `"
        << diff_md << "` |\n";
  }
  out << '\n';
}

void WriteWhatChanged(std::ofstream& out, const std::vector<PacketConfigAttempt>& attempts) {
  out << "## What Changed\n\n";
  for (const auto& attempt : attempts) {
    out << "- [`" << attempt.run_id << "`] changed `" << attempt.knob_path << "` from `"
        << attempt.before_value << "` to `" << attempt.after_value << "` using scenario `"
        << attempt.scenario_path.string() << "`.\n";
  }
  out << '\n';
}

void WriteRuledOut(std::ofstream& out, const std::vector<PacketConfigAttempt>& attempts,
                   const std::map<std::string, PacketRunEvidence>& evidence) {
  out << "## What We Ruled Out\n\n";

  bool wrote_any = false;
  for (const auto& attempt : attempts) {
    if (attempt.result != ResultStatus::kPass) {
      continue;
    }

    wrote_any = true;
    out << "- `" << attempt.knob_name << "` (`" << attempt.before_value << "` -> `"
        << attempt.after_value << "`) did not reproduce failure (run `" << attempt.run_id
        << "`).\n";

    const auto evidence_it = evidence.find(attempt.run_id);
    if (evidence_it != evidence.end()) {
      out << "  - run_json: `" << evidence_it->second.run_json_path.string() << "`\n";
      if (!evidence_it->second.diff_markdown_path.empty()) {
        out << "  - diff_md: `" << evidence_it->second.diff_markdown_path.string() << "`\n";
      }
    }
  }

  if (!wrote_any) {
    out << "- No configurations have been confidently ruled out yet.\n";
  }
  out << '\n';
}

void WriteRankedHypotheses(std::ofstream& out, const std::vector<HypothesisRank>& ranked,
                           const std::map<std::string, PacketRunEvidence>& evidence) {
  out << "## Ranked Hypotheses + Evidence Links\n\n";

  if (ranked.empty()) {
    out << "- No hypotheses recorded.\n\n";
    return;
  }

  std::size_t rank_index = 1;
  for (const auto& rank : ranked) {
    out << rank_index << ". [`" << rank.hypothesis->id << "`] score=" << rank.score << " status=`"
        << ToString(rank.hypothesis->status) << "` variable=`" << rank.hypothesis->variable_name
        << "`\n";
    out << "   - statement: " << rank.hypothesis->statement << "\n";
    out << "   - support_count: " << rank.support_count
        << ", contradiction_count: " << rank.contradiction_count << "\n";

    std::set<std::string> seen_runs;
    for (const ResultRow* row : rank.rows) {
      if (row == nullptr || row->evidence_run_id.empty()) {
        continue;
      }
      if (!seen_runs.insert(row->evidence_run_id).second) {
        continue;
      }

      out << "   - evidence run `" << row->evidence_run_id << "` result=`" << ToString(row->result)
          << "`\n";

      const auto evidence_it = evidence.find(row->evidence_run_id);
      if (evidence_it == evidence.end()) {
        out << "     - artifact links unavailable for this run id\n";
        continue;
      }

      WriteEvidenceLinks(out, evidence_it->second);
    }

    ++rank_index;
  }

  out << '\n';
}

} // namespace

bool WriteEngineerPacketMarkdown(const EngineerPacketInput& input, const fs::path& output_dir,
                                 fs::path& written_path, std::string& error) {
  if (!ValidateInput(input, error)) {
    return false;
  }
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  const std::vector<PacketConfigAttempt> attempts = SortedAttempts(input.configs_tried);
  const std::map<std::string, PacketRunEvidence> evidence = BuildEvidenceMap(input.run_evidence);
  const std::vector<HypothesisRank> ranked = RankHypotheses(*input.state);

  written_path = output_dir / "engineer_packet.md";
  std::ofstream out(written_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out << "# Engineer Packet\n\n";
  out << "## Run Context\n\n";
  out << "- session_id: `" << input.state->session_id << "`\n";
  out << "- scenario_id: `" << input.state->scenario_id << "`\n";
  out << "- symptom: `" << input.symptom << "`\n";
  out << "- baseline_scenario: `" << input.baseline_scenario_path.string() << "`\n";
  out << "- baseline_bundle: `" << input.baseline_bundle_dir.string() << "`\n";
  out << "- stop_reason: `" << ToString(input.stop_decision.reason) << "`\n";
  out << "- stop_explanation: " << input.stop_decision.explanation << "\n\n";

  WriteReproSteps(out, input, attempts, evidence);
  WriteConfigsTried(out, attempts, evidence);
  WriteWhatChanged(out, attempts);
  WriteRuledOut(out, attempts, evidence);
  WriteRankedHypotheses(out, ranked, evidence);

  if (!out) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::agent
