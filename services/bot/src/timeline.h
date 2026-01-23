#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mixer.h"
#include "vad.h"

struct TimelineEntry {
  std::string user_id;
  std::string display_name;
  int64_t start_ms = 0;
  int64_t end_ms = 0;
  float confidence = 0.0f;
};

struct TimelineResult {
  std::vector<TimelineEntry> entries;
};

TimelineResult BuildTimeline(const std::vector<MixTrack>& tracks,
                             int sample_rate,
                             const VadConfig& config);

bool WriteTimelineJson(const std::string& path,
                       const std::string& session_id,
                       int64_t t0_unix_ms,
                       const MixResult& mix,
                       const std::vector<MixTrack>& tracks,
                       const TimelineResult& timeline);

bool WriteTimelineText(const std::string& path,
                       const TimelineResult& timeline);
