#include "agent/variant_generator.hpp"

#include "agent/playbook.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace labops::agent {

namespace {

struct JsonValue {
  enum class Type {
    kObject,
    kArray,
    kString,
    kNumber,
    kBool,
    kNull,
  };

  using Object = std::map<std::string, JsonValue>;
  using Array = std::vector<JsonValue>;

  Type type = Type::kNull;
  Object object_value;
  Array array_value;
  std::string string_value;
  double number_value = 0.0;
  bool bool_value = false;
};

struct JsonParser {
  explicit JsonParser(std::string_view input) : input_(input) {}

  bool Parse(JsonValue& root, std::string& error) {
    SkipWhitespace();
    if (!ParseValue(root, error)) {
      return false;
    }
    SkipWhitespace();
    if (!AtEnd()) {
      return Fail("unexpected trailing content after JSON value", error);
    }
    return true;
  }

private:
  bool ParseValue(JsonValue& value, std::string& error) {
    if (AtEnd()) {
      return Fail("unexpected end of input while parsing value", error);
    }

    const char c = Peek();
    if (c == '{') {
      return ParseObject(value, error);
    }
    if (c == '[') {
      return ParseArray(value, error);
    }
    if (c == '"') {
      value.type = JsonValue::Type::kString;
      return ParseString(value.string_value, error);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
      value.type = JsonValue::Type::kNumber;
      return ParseNumber(value.number_value, error);
    }
    if (StartsWith("true")) {
      value.type = JsonValue::Type::kBool;
      value.bool_value = true;
      AdvanceN(4);
      return true;
    }
    if (StartsWith("false")) {
      value.type = JsonValue::Type::kBool;
      value.bool_value = false;
      AdvanceN(5);
      return true;
    }
    if (StartsWith("null")) {
      value.type = JsonValue::Type::kNull;
      AdvanceN(4);
      return true;
    }

    return Fail("expected JSON value", error);
  }

  bool ParseObject(JsonValue& value, std::string& error) {
    value = JsonValue{};
    value.type = JsonValue::Type::kObject;

    if (!ConsumeChar('{', "expected '{' to start object", error)) {
      return false;
    }
    SkipWhitespace();

    if (Match('}')) {
      return true;
    }

    while (true) {
      SkipWhitespace();
      std::string key;
      if (!ParseString(key, error)) {
        return false;
      }

      SkipWhitespace();
      if (!ConsumeChar(':', "expected ':' after object key", error)) {
        return false;
      }

      SkipWhitespace();
      JsonValue item;
      if (!ParseValue(item, error)) {
        return false;
      }
      value.object_value[key] = std::move(item);

      SkipWhitespace();
      if (Match('}')) {
        break;
      }
      if (!ConsumeChar(',', "expected ',' between object entries", error)) {
        return false;
      }
    }

    return true;
  }

  bool ParseArray(JsonValue& value, std::string& error) {
    value = JsonValue{};
    value.type = JsonValue::Type::kArray;

    if (!ConsumeChar('[', "expected '[' to start array", error)) {
      return false;
    }
    SkipWhitespace();

    if (Match(']')) {
      return true;
    }

    while (true) {
      SkipWhitespace();
      JsonValue item;
      if (!ParseValue(item, error)) {
        return false;
      }
      value.array_value.push_back(std::move(item));

      SkipWhitespace();
      if (Match(']')) {
        break;
      }
      if (!ConsumeChar(',', "expected ',' between array items", error)) {
        return false;
      }
    }

    return true;
  }

  bool ParseString(std::string& output, std::string& error) {
    output.clear();
    if (!ConsumeChar('"', "expected '\"' to start string", error)) {
      return false;
    }

    while (!AtEnd()) {
      const char c = Advance();
      if (c == '"') {
        return true;
      }
      if (c == '\\') {
        if (AtEnd()) {
          return Fail("unterminated escape sequence in string", error);
        }
        const char esc = Advance();
        switch (esc) {
        case '"':
        case '\\':
        case '/':
          output.push_back(esc);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        default:
          return Fail("invalid escape sequence in string", error);
        }
        continue;
      }

      if (static_cast<unsigned char>(c) < 0x20U) {
        return Fail("control character in string is not allowed", error);
      }
      output.push_back(c);
    }

    return Fail("unterminated string literal", error);
  }

  bool ParseNumber(double& output, std::string& error) {
    const std::size_t start = pos_;

    if (Match('-')) {
      // optional sign
    }

    if (Match('0')) {
      // single leading zero
    } else if (!ConsumeDigits()) {
      return Fail("expected digits in number", error);
    }

    if (Match('.')) {
      if (!ConsumeDigits()) {
        return Fail("expected digits after decimal point", error);
      }
    }

    if (Match('e') || Match('E')) {
      if (Match('+') || Match('-')) {
        // exponent sign
      }
      if (!ConsumeDigits()) {
        return Fail("expected exponent digits", error);
      }
    }

    const std::string text(input_.substr(start, pos_ - start));
    try {
      std::size_t parsed = 0;
      output = std::stod(text, &parsed);
      if (parsed != text.size()) {
        return Fail("invalid number token", error);
      }
    } catch (...) {
      return Fail("invalid numeric value", error);
    }

    return true;
  }

  void SkipWhitespace() {
    while (!AtEnd() && std::isspace(static_cast<unsigned char>(Peek())) != 0) {
      Advance();
    }
  }

  bool ConsumeDigits() {
    std::size_t count = 0;
    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
      Advance();
      ++count;
    }
    return count > 0U;
  }

  bool ConsumeChar(char expected, std::string_view message, std::string& error) {
    if (AtEnd() || Peek() != expected) {
      return Fail(message, error);
    }
    Advance();
    return true;
  }

  bool Match(char expected) {
    if (AtEnd() || Peek() != expected) {
      return false;
    }
    Advance();
    return true;
  }

  bool StartsWith(std::string_view token) const {
    if (pos_ + token.size() > input_.size()) {
      return false;
    }
    return input_.substr(pos_, token.size()) == token;
  }

  void AdvanceN(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      Advance();
    }
  }

  char Peek() const {
    return input_[pos_];
  }

  char Advance() {
    return input_[pos_++];
  }

  bool AtEnd() const {
    return pos_ >= input_.size();
  }

  bool Fail(std::string_view message, std::string& error) const {
    error = std::string(message) + " at byte offset " + std::to_string(pos_);
    return false;
  }

  std::string_view input_;
  std::size_t pos_ = 0;
};

std::string EscapeJson(std::string_view input) {
  std::ostringstream out;
  for (const char c : input) {
    switch (c) {
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
      const auto as_unsigned = static_cast<unsigned char>(c);
      if (as_unsigned < 0x20U) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(as_unsigned) << std::dec << std::setfill(' ');
      } else {
        out << c;
      }
      break;
    }
    }
  }
  return out.str();
}

void SerializeJson(const JsonValue& value, std::ostringstream& out);

void SerializeObject(const JsonValue::Object& object_value, std::ostringstream& out) {
  out << "{";
  bool first = true;
  for (const auto& [key, value] : object_value) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << '"' << EscapeJson(key) << "\":";
    SerializeJson(value, out);
  }
  out << "}";
}

void SerializeArray(const JsonValue::Array& array_value, std::ostringstream& out) {
  out << "[";
  for (std::size_t i = 0; i < array_value.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    SerializeJson(array_value[i], out);
  }
  out << "]";
}

void SerializeJson(const JsonValue& value, std::ostringstream& out) {
  switch (value.type) {
  case JsonValue::Type::kObject:
    SerializeObject(value.object_value, out);
    return;
  case JsonValue::Type::kArray:
    SerializeArray(value.array_value, out);
    return;
  case JsonValue::Type::kString:
    out << '"' << EscapeJson(value.string_value) << '"';
    return;
  case JsonValue::Type::kNumber: {
    const double rounded = std::round(value.number_value);
    if (std::abs(value.number_value - rounded) < 1e-9) {
      out << static_cast<std::int64_t>(rounded);
    } else {
      out << std::setprecision(12) << value.number_value;
    }
    return;
  }
  case JsonValue::Type::kBool:
    out << (value.bool_value ? "true" : "false");
    return;
  case JsonValue::Type::kNull:
    out << "null";
    return;
  }
}

std::string ToJson(const JsonValue& value) {
  std::ostringstream out;
  SerializeJson(value, out);
  return out.str();
}

bool ReadFile(const fs::path& path, std::string& contents, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "failed to open file: " + path.string();
    return false;
  }
  contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

bool WriteFile(const fs::path& path, std::string_view contents, std::string& error) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "failed to open output file: " + path.string();
    return false;
  }
  file << contents << '\n';
  if (!file) {
    error = "failed while writing output file: " + path.string();
    return false;
  }
  return true;
}

bool ValidateRequest(const VariantGenerationRequest& request, std::string& error) {
  if (request.base_scenario_path.empty()) {
    error = "base scenario path cannot be empty";
    return false;
  }
  if (request.symptom.empty()) {
    error = "symptom cannot be empty";
    return false;
  }
  if (request.output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  const fs::path base_path(request.base_scenario_path);
  std::error_code ec;
  if (!fs::exists(base_path, ec) || ec) {
    error = "base scenario file not found: " + request.base_scenario_path;
    return false;
  }
  if (!fs::is_regular_file(base_path, ec) || ec) {
    error = "base scenario must be a regular file: " + request.base_scenario_path;
    return false;
  }
  if (base_path.extension() != ".json") {
    error = "base scenario must use .json extension: " + request.base_scenario_path;
    return false;
  }

  return true;
}

JsonValue* FindObjectMember(JsonValue& object_value, std::string_view key) {
  if (object_value.type != JsonValue::Type::kObject) {
    return nullptr;
  }
  auto it = object_value.object_value.find(std::string(key));
  if (it == object_value.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

const JsonValue* FindObjectMember(const JsonValue& object_value, std::string_view key) {
  if (object_value.type != JsonValue::Type::kObject) {
    return nullptr;
  }
  auto it = object_value.object_value.find(std::string(key));
  if (it == object_value.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

JsonValue* EnsureObjectMember(JsonValue& object_value, std::string_view key) {
  if (object_value.type != JsonValue::Type::kObject) {
    object_value = JsonValue{};
    object_value.type = JsonValue::Type::kObject;
  }

  auto [it, inserted] = object_value.object_value.emplace(std::string(key), JsonValue{});
  if (inserted) {
    it->second.type = JsonValue::Type::kObject;
  }
  return &it->second;
}

JsonValue MakeNumber(std::int64_t value) {
  JsonValue number;
  number.type = JsonValue::Type::kNumber;
  number.number_value = static_cast<double>(value);
  return number;
}

JsonValue MakeString(std::string value) {
  JsonValue string_value;
  string_value.type = JsonValue::Type::kString;
  string_value.string_value = std::move(value);
  return string_value;
}

std::string FormatInt(std::int64_t value) {
  return std::to_string(value);
}

std::optional<std::int64_t> ReadIntegerAtPath(const JsonValue& root,
                                              std::initializer_list<std::string_view> path) {
  const JsonValue* cursor = &root;
  for (const std::string_view key : path) {
    cursor = FindObjectMember(*cursor, key);
    if (cursor == nullptr) {
      return std::nullopt;
    }
  }
  if (cursor->type != JsonValue::Type::kNumber) {
    return std::nullopt;
  }
  const double rounded = std::round(cursor->number_value);
  if (std::abs(cursor->number_value - rounded) > 1e-9) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(rounded);
}

void SetIntegerAtPath(JsonValue& root, std::initializer_list<std::string_view> path,
                      std::int64_t value) {
  JsonValue* cursor = &root;
  auto it = path.begin();
  for (; it != path.end(); ++it) {
    const bool is_leaf = std::next(it) == path.end();
    if (is_leaf) {
      if (cursor->type != JsonValue::Type::kObject) {
        cursor->type = JsonValue::Type::kObject;
        cursor->object_value.clear();
      }
      cursor->object_value[std::string(*it)] = MakeNumber(value);
      return;
    }

    cursor = EnsureObjectMember(*cursor, *it);
  }
}

std::int64_t ClampInt(std::int64_t value, std::int64_t low, std::int64_t high) {
  return std::max(low, std::min(high, value));
}

std::string SanitizeFilenameToken(std::string_view input) {
  std::string out;
  out.reserve(input.size());

  for (const char c : input) {
    const bool allowed =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (allowed) {
      out.push_back(c);
      continue;
    }
    out.push_back('_');
  }

  if (out.empty()) {
    return "variant";
  }
  return out;
}

bool ApplyPacketDelayMutation(const JsonValue& base, JsonValue& variant,
                              ScenarioVariant& scenario_variant, std::string& error) {
  const std::int64_t base_us =
      ReadIntegerAtPath(base, {"camera", "network", "inter_packet_delay_us"}).value_or(0);
  const std::int64_t candidate_us = base_us + 5'000;
  SetIntegerAtPath(variant, {"camera", "network", "inter_packet_delay_us"}, candidate_us);

  scenario_variant.knob_path = "camera.network.inter_packet_delay_us";
  scenario_variant.before_value = FormatInt(base_us);
  scenario_variant.after_value = FormatInt(candidate_us);
  error.clear();
  return true;
}

bool ApplyFpsMutation(const JsonValue& base, JsonValue& variant, ScenarioVariant& scenario_variant,
                      std::string& error) {
  const std::int64_t base_fps = ReadIntegerAtPath(base, {"camera", "fps"}).value_or(30);
  const std::int64_t candidate_fps = std::max<std::int64_t>(1, base_fps - 1);
  SetIntegerAtPath(variant, {"camera", "fps"}, candidate_fps);

  scenario_variant.knob_path = "camera.fps";
  scenario_variant.before_value = FormatInt(base_fps);
  scenario_variant.after_value = FormatInt(candidate_fps);
  error.clear();
  return true;
}

bool ApplyRoiToggleMutation(const JsonValue& base, JsonValue& variant,
                            ScenarioVariant& scenario_variant, std::string& error) {
  JsonValue* camera = EnsureObjectMember(variant, "camera");
  if (camera->type != JsonValue::Type::kObject) {
    camera->type = JsonValue::Type::kObject;
    camera->object_value.clear();
  }

  const JsonValue* base_camera = FindObjectMember(base, "camera");
  const bool has_base_roi =
      (base_camera != nullptr) && (FindObjectMember(*base_camera, "roi") != nullptr);

  if (has_base_roi) {
    camera->object_value.erase("roi");
    scenario_variant.knob_path = "camera.roi";
    scenario_variant.before_value = "enabled";
    scenario_variant.after_value = "disabled";
    error.clear();
    return true;
  }

  const std::int64_t width = ReadIntegerAtPath(base, {"camera", "width"}).value_or(1920);
  const std::int64_t height = ReadIntegerAtPath(base, {"camera", "height"}).value_or(1080);

  JsonValue roi;
  roi.type = JsonValue::Type::kObject;
  roi.object_value["x"] = MakeNumber(0);
  roi.object_value["y"] = MakeNumber(0);
  roi.object_value["width"] = MakeNumber(std::max<std::int64_t>(1, width / 2));
  roi.object_value["height"] = MakeNumber(std::max<std::int64_t>(1, height / 2));
  camera->object_value["roi"] = std::move(roi);

  scenario_variant.knob_path = "camera.roi";
  scenario_variant.before_value = "disabled";
  scenario_variant.after_value = "enabled";
  error.clear();
  return true;
}

bool ApplyReorderMutation(const JsonValue& base, JsonValue& variant,
                          ScenarioVariant& scenario_variant, std::string& error) {
  const std::int64_t base_reorder = ReadIntegerAtPath(base, {"sim_faults", "reorder"}).value_or(0);
  const std::int64_t candidate_reorder = ClampInt(base_reorder + 5, 0, 100);
  SetIntegerAtPath(variant, {"sim_faults", "reorder"}, candidate_reorder);

  scenario_variant.knob_path = "sim_faults.reorder";
  scenario_variant.before_value = FormatInt(base_reorder);
  scenario_variant.after_value = FormatInt(candidate_reorder);
  error.clear();
  return true;
}

bool ApplyLossMutation(const JsonValue& base, JsonValue& variant, ScenarioVariant& scenario_variant,
                       std::string& error) {
  const std::int64_t base_drop =
      ReadIntegerAtPath(base, {"sim_faults", "drop_percent"}).value_or(0);
  const std::int64_t candidate_drop = (base_drop >= 100) ? 90 : ClampInt(base_drop + 10, 0, 100);
  SetIntegerAtPath(variant, {"sim_faults", "drop_percent"}, candidate_drop);

  scenario_variant.knob_path = "sim_faults.drop_percent";
  scenario_variant.before_value = FormatInt(base_drop);
  scenario_variant.after_value = FormatInt(candidate_drop);
  error.clear();
  return true;
}

bool ApplyKnobMutation(const JsonValue& base, JsonValue& variant, std::string_view knob_name,
                       ScenarioVariant& scenario_variant, std::string& error) {
  if (knob_name == "packet_delay_ms") {
    return ApplyPacketDelayMutation(base, variant, scenario_variant, error);
  }
  if (knob_name == "fps") {
    return ApplyFpsMutation(base, variant, scenario_variant, error);
  }
  if (knob_name == "roi_enabled") {
    return ApplyRoiToggleMutation(base, variant, scenario_variant, error);
  }
  if (knob_name == "reorder_percent") {
    return ApplyReorderMutation(base, variant, scenario_variant, error);
  }
  if (knob_name == "loss_percent") {
    return ApplyLossMutation(base, variant, scenario_variant, error);
  }

  error = "unsupported playbook knob for scenario variant generation: " + std::string(knob_name);
  return false;
}

std::string BuildVariantFileName(const fs::path& base_path, std::string_view knob_name) {
  return base_path.stem().string() + "__" + SanitizeFilenameToken(knob_name) + ".json";
}

std::string BuildVariantManifestJson(const VariantGenerationResult& result) {
  JsonValue root;
  root.type = JsonValue::Type::kObject;
  root.object_value["playbook_id"] = MakeString(result.playbook_id);
  root.object_value["output_dir"] = MakeString(result.output_dir.string());

  JsonValue variants;
  variants.type = JsonValue::Type::kArray;
  for (const auto& variant : result.variants) {
    JsonValue item;
    item.type = JsonValue::Type::kObject;
    item.object_value["knob_name"] = MakeString(variant.knob_name);
    item.object_value["knob_path"] = MakeString(variant.knob_path);
    item.object_value["before_value"] = MakeString(variant.before_value);
    item.object_value["after_value"] = MakeString(variant.after_value);
    item.object_value["scenario_path"] = MakeString(variant.scenario_path.string());
    variants.array_value.push_back(std::move(item));
  }

  root.object_value["variants"] = std::move(variants);
  return ToJson(root);
}

} // namespace

bool OaatVariantGenerator::Generate(const VariantGenerationRequest& request,
                                    VariantGenerationResult& result, std::string& error) const {
  result = VariantGenerationResult{};
  error.clear();

  if (!ValidateRequest(request, error)) {
    return false;
  }

  std::string base_text;
  if (!ReadFile(fs::path(request.base_scenario_path), base_text, error)) {
    return false;
  }

  JsonValue base_root;
  JsonParser parser(base_text);
  if (!parser.Parse(base_root, error)) {
    error = "failed to parse base scenario JSON: " + error;
    return false;
  }
  if (base_root.type != JsonValue::Type::kObject) {
    error = "base scenario JSON root must be an object";
    return false;
  }

  Playbook playbook;
  if (!SelectPlaybookForSymptom(request.symptom, playbook, error)) {
    return false;
  }

  std::error_code ec;
  const fs::path output_dir = fs::absolute(request.output_dir, ec);
  if (ec) {
    error = "failed to resolve output directory: " + request.output_dir.string();
    return false;
  }

  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }

  result.playbook_id = playbook.id;
  result.output_dir = output_dir;

  const fs::path base_path(request.base_scenario_path);
  for (const auto& knob : playbook.knobs) {
    JsonValue variant_root = base_root;

    ScenarioVariant scenario_variant;
    scenario_variant.knob_name = knob.name;

    std::string mutation_error;
    if (!ApplyKnobMutation(base_root, variant_root, knob.name, scenario_variant, mutation_error)) {
      error = mutation_error;
      return false;
    }

    const fs::path variant_path = output_dir / BuildVariantFileName(base_path, knob.name);
    if (!WriteFile(variant_path, ToJson(variant_root), error)) {
      return false;
    }

    scenario_variant.scenario_path = variant_path;
    result.variants.push_back(std::move(scenario_variant));
  }

  result.manifest_path = request.output_dir / "variants_manifest.json";
  if (!WriteFile(result.manifest_path, BuildVariantManifestJson(result), error)) {
    return false;
  }

  return true;
}

} // namespace labops::agent
