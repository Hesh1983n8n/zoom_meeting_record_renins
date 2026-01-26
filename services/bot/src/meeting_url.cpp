#include "meeting_url.h"

#include <regex>

std::optional<MeetingUrlInfo> ParseMeetingUrl(const std::string& url) {
  std::regex re(R"(https?://[^/]+/j/([0-9]+)(\?[^#]+)?)");
  std::smatch match;
  if (!std::regex_search(url, match, re)) {
    return std::nullopt;
  }

  MeetingUrlInfo info;
  info.meeting_number = match[1].str();
  std::string query = match.size() > 2 ? match[2].str() : std::string();
  std::regex pwd_re(R"(pwd=([^&]+))");
  std::smatch pwd_match;
  if (std::regex_search(query, pwd_match, pwd_re)) {
    info.pwd = pwd_match[1].str();
  }
  return info;
}
