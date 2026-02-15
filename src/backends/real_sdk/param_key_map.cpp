#include "backends/real_sdk/param_key_map.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::backends::real_sdk {

namespace {

void SkipWhitespace(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
}

bool ParseJsonString(std::string_view text, std::size_t& cursor, std::string& value,
                     std::string& error) {
  if (cursor >= text.size() || text[cursor] != '"') {
    error = "expected opening quote for string at offset " + std::to_string(cursor);
    return false;
  }
  ++cursor;

  std::string out;
  while (cursor < text.size()) {
    const char c = text[cursor++];
    if (c == '"') {
      value = std::move(out);
      return true;
    }
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (cursor >= text.size()) {
      error = "unterminated escape sequence at end of input";
      return false;
    }

    const char escaped = text[cursor++];
    switch (escaped) {
    case '"':
    case '\\':
    case '/':
      out.push_back(escaped);
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    default:
      error = std::string("unsupported escape sequence \\") + escaped + " at offset " +
              std::to_string(cursor - 1);
      return false;
    }
  }

  error = "unterminated string literal";
  return false;
}

bool ParseStringObject(std::string_view text, std::map<std::string, std::string, std::less<>>& out,
                       std::string& error) {
  out.clear();
  std::size_t cursor = 0;
  SkipWhitespace(text, cursor);
  if (cursor >= text.size() || text[cursor] != '{') {
    error = "param key map must start with '{'";
    return false;
  }
  ++cursor;

  while (true) {
    SkipWhitespace(text, cursor);
    if (cursor >= text.size()) {
      error = "unexpected end of input while parsing object";
      return false;
    }
    if (text[cursor] == '}') {
      ++cursor;
      break;
    }

    std::string key;
    if (!ParseJsonString(text, cursor, key, error)) {
      return false;
    }
    if (key.empty()) {
      error = "mapping key must not be empty";
      return false;
    }

    SkipWhitespace(text, cursor);
    if (cursor >= text.size() || text[cursor] != ':') {
      error = "expected ':' after key '" + key + "'";
      return false;
    }
    ++cursor;

    SkipWhitespace(text, cursor);
    std::string value;
    if (!ParseJsonString(text, cursor, value, error)) {
      return false;
    }
    if (value.empty()) {
      error = "mapping value for key '" + key + "' must not be empty";
      return false;
    }

    if (!out.emplace(key, value).second) {
      error = "duplicate mapping key: " + key;
      return false;
    }

    SkipWhitespace(text, cursor);
    if (cursor >= text.size()) {
      error = "unexpected end of input after key '" + key + "'";
      return false;
    }
    if (text[cursor] == ',') {
      ++cursor;
      continue;
    }
    if (text[cursor] == '}') {
      ++cursor;
      break;
    }

    error = std::string("expected ',' or '}' at offset ") + std::to_string(cursor);
    return false;
  }

  SkipWhitespace(text, cursor);
  if (cursor != text.size()) {
    error = "unexpected trailing content after JSON object";
    return false;
  }
  return true;
}

} // namespace

bool ParamKeyMap::Has(std::string_view generic_key) const {
  return generic_to_node.find(generic_key) != generic_to_node.end();
}

bool ParamKeyMap::Resolve(std::string_view generic_key, std::string& node_name) const {
  const auto it = generic_to_node.find(generic_key);
  if (it == generic_to_node.end()) {
    return false;
  }
  node_name = it->second;
  return true;
}

std::vector<std::string> ParamKeyMap::ListGenericKeys() const {
  std::vector<std::string> keys;
  keys.reserve(generic_to_node.size());
  for (const auto& [key, _] : generic_to_node) {
    keys.push_back(key);
  }
  return keys;
}

bool LoadParamKeyMapFromText(std::string_view json_text, ParamKeyMap& map, std::string& error) {
  map = ParamKeyMap{};
  error.clear();

  if (!ParseStringObject(json_text, map.generic_to_node, error)) {
    return false;
  }
  if (map.generic_to_node.empty()) {
    error = "param key map must include at least one key mapping";
    return false;
  }
  return true;
}

bool LoadParamKeyMapFromFile(const fs::path& path, ParamKeyMap& map, std::string& error) {
  map = ParamKeyMap{};
  error.clear();

  if (path.empty()) {
    error = "param key map path cannot be empty";
    return false;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "failed to open param key map file: " + path.string();
    return false;
  }

  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (text.empty()) {
    error = "param key map file is empty: " + path.string();
    return false;
  }
  if (!LoadParamKeyMapFromText(text, map, error)) {
    error = "failed to parse param key map '" + path.string() + "': " + error;
    return false;
  }
  return true;
}

fs::path ResolveDefaultParamKeyMapPath() {
  if (const char* env = std::getenv("LABOPS_PARAM_KEY_MAP"); env != nullptr && *env != '\0') {
    return fs::path(env);
  }

  const fs::path relative =
      fs::path("src") / "backends" / "real_sdk" / "maps" / "param_key_map.json";
  std::error_code ec;
  fs::path cursor = fs::current_path(ec);
  if (ec) {
    return relative;
  }

  for (std::size_t depth = 0; depth < 12U; ++depth) {
    const fs::path candidate = cursor / relative;
    if (fs::exists(candidate, ec) && !ec && fs::is_regular_file(candidate, ec) && !ec) {
      return candidate;
    }
    if (!cursor.has_parent_path()) {
      break;
    }
    const fs::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }
    cursor = parent;
  }

  return relative;
}

} // namespace labops::backends::real_sdk
