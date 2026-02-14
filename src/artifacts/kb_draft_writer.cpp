#include "artifacts/kb_draft_writer.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

bool ReadTextFile(const fs::path& file_path, std::string& text, std::string& error) {
  text.clear();
  error.clear();

  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    error = "failed to open file '" + file_path.string() + "'";
    return false;
  }

  text.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (!input.good() && !input.eof()) {
    error = "failed while reading file '" + file_path.string() + "'";
    return false;
  }

  return true;
}

std::string Trim(std::string_view value) {
  std::size_t start = 0;
  while (start < value.size() &&
         (value[start] == ' ' || value[start] == '\t' || value[start] == '\n' ||
          value[start] == '\r')) {
    ++start;
  }

  std::size_t end = value.size();
  while (end > start &&
         (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\n' ||
          value[end - 1] == '\r')) {
    --end;
  }

  return std::string(value.substr(start, end - start));
}

fs::path ResolveEngineerPacketPath(const fs::path& run_dir) {
  const fs::path direct = run_dir / "engineer_packet.md";
  if (fs::exists(direct) && fs::is_regular_file(direct)) {
    return direct;
  }

  const fs::path nested = run_dir / "packet" / "engineer_packet.md";
  if (fs::exists(nested) && fs::is_regular_file(nested)) {
    return nested;
  }

  return {};
}

std::string ExtractMarkdownSection(const std::string& markdown, std::string_view heading) {
  // Engineer packet sections use stable `## <heading>` markers, so a bounded
  // string search keeps extraction deterministic without pulling in a parser.
  const std::string marker = "## " + std::string(heading);
  const std::size_t marker_pos = markdown.find(marker);
  if (marker_pos == std::string::npos) {
    return "";
  }

  const std::size_t start_pos = markdown.find('\n', marker_pos);
  if (start_pos == std::string::npos) {
    return "";
  }

  const std::size_t section_start = start_pos + 1;
  const std::size_t section_end = markdown.find("\n## ", section_start);
  if (section_end == std::string::npos) {
    return Trim(markdown.substr(section_start));
  }
  return Trim(markdown.substr(section_start, section_end - section_start));
}

std::string ExtractRunContextValue(const std::string& run_context, std::string_view key) {
  std::istringstream lines(run_context);
  std::string line;
  const std::string prefix = "- " + std::string(key) + ":";
  while (std::getline(lines, line)) {
    if (line.rfind(prefix, 0U) != 0U) {
      continue;
    }
    const std::size_t first_tick = line.find('`');
    if (first_tick != std::string::npos) {
      const std::size_t second_tick = line.find('`', first_tick + 1);
      if (second_tick != std::string::npos && second_tick > first_tick + 1) {
        return line.substr(first_tick + 1, second_tick - first_tick - 1);
      }
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos || colon + 1 >= line.size()) {
      return "";
    }
    return Trim(line.substr(colon + 1));
  }
  return "";
}

std::string ExtractFirstHypothesisBlock(const std::string& ranked_hypotheses) {
  // Keep only the top-ranked hypothesis block so the KB draft starts from the
  // strongest currently-supported lead.
  std::istringstream lines(ranked_hypotheses);
  std::string line;
  std::ostringstream block;
  bool collecting = false;
  while (std::getline(lines, line)) {
    if (!collecting) {
      if (line.rfind("1. ", 0U) == 0U) {
        collecting = true;
        block << line << '\n';
      }
      continue;
    }

    if (line.rfind("2. ", 0U) == 0U) {
      break;
    }
    block << line << '\n';
  }
  return Trim(block.str());
}

std::string ExtractLikelyCauseSummary(const std::string& first_hypothesis_block) {
  if (first_hypothesis_block.empty()) {
    return "";
  }
  const std::size_t statement_pos = first_hypothesis_block.find("statement:");
  if (statement_pos == std::string::npos) {
    return "";
  }
  const std::size_t value_start = statement_pos + std::string("statement:").size();
  const std::size_t line_end = first_hypothesis_block.find('\n', value_start);
  if (line_end == std::string::npos) {
    return Trim(first_hypothesis_block.substr(value_start));
  }
  return Trim(first_hypothesis_block.substr(value_start, line_end - value_start));
}

void WriteOptionalEvidencePath(std::ofstream& out, std::string_view label, const fs::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  if (!fs::exists(path, ec) || ec) {
    return;
  }
  out << "- " << label << ": `" << path.string() << "`\n";
}

} // namespace

bool WriteKbDraftFromRunFolder(const fs::path& run_dir, const fs::path& output_path,
                               fs::path& written_path, std::string& error) {
  written_path.clear();
  error.clear();

  if (run_dir.empty()) {
    error = "run folder path cannot be empty";
    return false;
  }
  if (output_path.empty()) {
    error = "output path cannot be empty";
    return false;
  }

  std::error_code ec;
  if (!fs::exists(run_dir, ec) || ec || !fs::is_directory(run_dir, ec)) {
    error = "run folder does not exist or is not a directory: " + run_dir.string();
    return false;
  }

  const fs::path engineer_packet_path = ResolveEngineerPacketPath(run_dir);
  if (engineer_packet_path.empty()) {
    error = "engineer_packet.md not found under run folder: " + run_dir.string();
    return false;
  }

  std::string engineer_packet_text;
  if (!ReadTextFile(engineer_packet_path, engineer_packet_text, error)) {
    return false;
  }

  const std::string run_context = ExtractMarkdownSection(engineer_packet_text, "Run Context");
  const std::string repro_steps = ExtractMarkdownSection(engineer_packet_text, "Repro Steps");
  const std::string ruled_out = ExtractMarkdownSection(engineer_packet_text, "What We Ruled Out");
  const std::string ranked_hypotheses =
      ExtractMarkdownSection(engineer_packet_text, "Ranked Hypotheses + Evidence Links");
  const std::string first_hypothesis = ExtractFirstHypothesisBlock(ranked_hypotheses);
  const std::string likely_cause = ExtractLikelyCauseSummary(first_hypothesis);
  const std::string scenario_id = ExtractRunContextValue(run_context, "scenario_id");
  const std::string symptom = ExtractRunContextValue(run_context, "symptom");
  const std::string stop_reason = ExtractRunContextValue(run_context, "stop_reason");

  fs::path normalized_output_path = output_path;
  if (fs::exists(normalized_output_path, ec) && !ec && fs::is_directory(normalized_output_path, ec)) {
    normalized_output_path /= "kb_draft.md";
  }

  const fs::path output_parent = normalized_output_path.parent_path();
  if (!output_parent.empty()) {
    fs::create_directories(output_parent, ec);
    if (ec) {
      error = "failed to create kb draft output directory '" + output_parent.string() + "': " +
              ec.message();
      return false;
    }
  }

  std::ofstream out(normalized_output_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "failed to open kb draft output file '" + normalized_output_path.string() + "'";
    return false;
  }

  out << "# KB Draft: " << run_dir.filename().string() << "\n\n";
  out << "## Status\n\n";
  out << "- draft_state: `needs_review`\n";
  out << "- scenario_id: `" << scenario_id << "`\n";
  out << "- symptom: `" << symptom << "`\n";
  out << "- stop_reason: `" << stop_reason << "`\n\n";

  out << "## Problem Summary\n\n";
  if (!likely_cause.empty()) {
    out << likely_cause << "\n\n";
  } else {
    out << "_Summarize the customer/user-visible issue in one paragraph._\n\n";
  }

  out << "## Scope and Impact\n\n";
  out << "- _Describe affected camera models, firmware versions, and environments._\n";
  out << "- _Describe impact severity and frequency._\n\n";

  out << "## Reproduction (Source: Engineer Packet)\n\n";
  if (!repro_steps.empty()) {
    out << repro_steps << "\n\n";
  } else {
    out << "_No repro steps were found in the engineer packet. Fill in manually._\n\n";
  }

  out << "## Findings and Likely Cause\n\n";
  if (!first_hypothesis.empty()) {
    out << first_hypothesis << "\n\n";
  } else {
    out << "_No ranked hypothesis block found. Add cause analysis manually._\n\n";
  }

  out << "## What We Ruled Out\n\n";
  if (!ruled_out.empty()) {
    out << ruled_out << "\n\n";
  } else {
    out << "_No ruled-out section found. Add ruled-out paths manually._\n\n";
  }

  out << "## Resolution or Mitigation\n\n";
  out << "- _Describe exact fix, workaround, or rollback guidance._\n";
  out << "- _List config changes users should apply._\n\n";

  out << "## Validation After Fix\n\n";
  out << "- _List verification commands/runs and outcomes._\n";
  out << "- _Include baseline compare results after fix._\n\n";

  out << "## Evidence Links\n\n";
  out << "- run_folder: `" << run_dir.string() << "`\n";
  out << "- engineer_packet: `" << engineer_packet_path.string() << "`\n";
  WriteOptionalEvidencePath(out, "summary", run_dir / "summary.md");
  WriteOptionalEvidencePath(out, "report_html", run_dir / "report.html");
  WriteOptionalEvidencePath(out, "run_json", run_dir / "run.json");
  WriteOptionalEvidencePath(out, "events_jsonl", run_dir / "events.jsonl");
  WriteOptionalEvidencePath(out, "metrics_json", run_dir / "metrics.json");
  WriteOptionalEvidencePath(out, "metrics_csv", run_dir / "metrics.csv");
  WriteOptionalEvidencePath(out, "diff_md", run_dir / "diff.md");
  WriteOptionalEvidencePath(out, "diff_json", run_dir / "diff.json");
  out << '\n';

  out << "## Publication Checklist\n\n";
  out << "- [ ] Remove confidential host/user identifiers\n";
  out << "- [ ] Confirm repro steps are deterministic\n";
  out << "- [ ] Add owner + review date\n";
  out << "- [ ] Link related issue/ticket\n";

  if (!out) {
    error = "failed while writing kb draft output '" + normalized_output_path.string() + "'";
    return false;
  }

  written_path = normalized_output_path;
  return true;
}

} // namespace labops::artifacts
