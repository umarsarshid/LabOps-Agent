#ifndef LABOPS_CORE_JSON_UTILS_HPP_
#define LABOPS_CORE_JSON_UTILS_HPP_

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace labops::core {

// Shared JSON string escaping for artifact/event/schema writers.
// Keeping one implementation avoids subtle formatting drift across outputs.
inline std::string EscapeJson(std::string_view input) {
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

} // namespace labops::core

#endif // LABOPS_CORE_JSON_UTILS_HPP_
