#pragma once

#include <optional>
#include <string>

struct MeetingUrlInfo {
  std::string meeting_number;
  std::optional<std::string> pwd;
};

std::optional<MeetingUrlInfo> ParseMeetingUrl(const std::string& url);
