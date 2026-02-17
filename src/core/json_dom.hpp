#ifndef LABOPS_CORE_JSON_DOM_HPP_
#define LABOPS_CORE_JSON_DOM_HPP_

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace labops::core::json {

// Minimal DOM used by scenario validation and agent variant generation.
// The type is intentionally small and STL-only so modules can share one parser
// without introducing external JSON dependencies.
struct Value {
  enum class Type {
    kObject,
    kArray,
    kString,
    kNumber,
    kBool,
    kNull,
  };

  using Object = std::map<std::string, Value>;
  using Array = std::vector<Value>;

  Type type = Type::kNull;
  Object object_value;
  Array array_value;
  std::string string_value;
  double number_value = 0.0;
  bool bool_value = false;
};

// Lightweight JSON parser with deterministic diagnostics.
// Errors report line/column so malformed fixtures are actionable in CI and
// local iteration loops.
class Parser {
public:
  explicit Parser(std::string_view input) : input_(input) {}

  bool Parse(Value& root, std::string& error) {
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
  bool ParseValue(Value& value, std::string& error) {
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
      value.type = Value::Type::kString;
      return ParseString(value.string_value, error);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
      value.type = Value::Type::kNumber;
      return ParseNumber(value.number_value, error);
    }
    if (StartsWith("true")) {
      value.type = Value::Type::kBool;
      value.bool_value = true;
      AdvanceN(4);
      return true;
    }
    if (StartsWith("false")) {
      value.type = Value::Type::kBool;
      value.bool_value = false;
      AdvanceN(5);
      return true;
    }
    if (StartsWith("null")) {
      value.type = Value::Type::kNull;
      AdvanceN(4);
      return true;
    }

    return Fail("expected JSON value", error);
  }

  bool ParseObject(Value& value, std::string& error) {
    value = Value{};
    value.type = Value::Type::kObject;

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
      Value item;
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

  bool ParseArray(Value& value, std::string& error) {
    value = Value{};
    value.type = Value::Type::kArray;

    if (!ConsumeChar('[', "expected '[' to start array", error)) {
      return false;
    }
    SkipWhitespace();

    if (Match(']')) {
      return true;
    }

    while (true) {
      SkipWhitespace();
      Value item;
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
        case 'u':
          return Fail("unicode escape \\uXXXX is not supported in current parser", error);
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
    } else {
      if (!ConsumeDigits()) {
        return Fail("expected digits in number", error);
      }
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
    const char c = input_[pos_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }

  bool AtEnd() const {
    return pos_ >= input_.size();
  }

  bool Fail(std::string_view message, std::string& error) const {
    error = "parse error at line " + std::to_string(line_) + ", col " + std::to_string(col_) +
            ": " + std::string(message);
    return false;
  }

  std::string_view input_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t col_ = 1;
};

inline bool Parse(std::string_view input, Value& root, std::string& error) {
  Parser parser(input);
  return parser.Parse(root, error);
}

} // namespace labops::core::json

#endif // LABOPS_CORE_JSON_DOM_HPP_
