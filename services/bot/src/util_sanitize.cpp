#include "util_sanitize.h"

#include <cctype>

std::string SanitizeFilename(const std::string& input, size_t max_len) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
      out.push_back(c);
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = "participant";
  }
  if (out.size() > max_len) {
    out.resize(max_len);
  }
  return out;
}
