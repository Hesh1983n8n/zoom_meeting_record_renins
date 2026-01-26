#include "timeline.h"

#include <algorithm>
#include <fstream>
#include <sstream>

TimelineResult BuildTimeline(const std::vector<MixTrack>& tracks,
                             int sample_rate,
                             const VadConfig& config) {
  TimelineResult result;
  for (const auto& track : tracks) {
    std::vector<int16_t> pcm;
    for (const auto& chunk : track.chunks) {
      if (chunk.sample_rate != sample_rate || chunk.pcm.empty()) {
        continue;
      }
      std::vector<int16_t> mono = chunk.pcm;
      if (chunk.channels > 1) {
        size_t frames = chunk.pcm.size() / chunk.channels;
        mono.resize(frames);
        for (size_t i = 0; i < frames; ++i) {
          int32_t sum = 0;
          for (int c = 0; c < chunk.channels; ++c) {
            sum += chunk.pcm[i * chunk.channels + c];
          }
          mono[i] = static_cast<int16_t>(sum / chunk.channels);
        }
      }
      int64_t start_samples = static_cast<int64_t>((chunk.start_ms * sample_rate) / 1000);
      if (start_samples > 0) {
        pcm.resize(static_cast<size_t>(start_samples), 0);
      }
      pcm.insert(pcm.end(), mono.begin(), mono.end());
    }

    auto segments = ComputeVadSegments(pcm, sample_rate, config);
    for (const auto& seg : segments) {
      size_t start_idx = static_cast<size_t>((seg.start_ms * sample_rate) / 1000);
      size_t end_idx = static_cast<size_t>((seg.end_ms * sample_rate) / 1000);
      float confidence = ComputeSegmentConfidence(pcm, start_idx, end_idx);
      result.entries.push_back({track.user_id, track.display_name, seg.start_ms, seg.end_ms, confidence});
    }
  }

  std::sort(result.entries.begin(), result.entries.end(), [](const TimelineEntry& a, const TimelineEntry& b) {
    return a.start_ms < b.start_ms;
  });
  return result;
}

bool WriteTimelineJson(const std::string& path,
                       const std::string& session_id,
                       int64_t t0_unix_ms,
                       const MixResult& mix,
                       const std::vector<MixTrack>& tracks,
                       const TimelineResult& timeline) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }

  out << "{\n";
  out << "  \"session_id\": \"" << session_id << "\",\n";
  out << "  \"t0_unix_ms\": " << t0_unix_ms << ",\n";
  out << "  \"final_mix\": {\n";
  out << "    \"path\": \"final/final_mix.wav\",\n";
  out << "    \"sample_rate\": " << mix.sample_rate << ",\n";
  out << "    \"channels\": " << mix.channels << ",\n";
  out << "    \"duration_ms\": " << mix.duration_ms << "\n";
  out << "  },\n";
  out << "  \"participants\": [\n";
  for (size_t i = 0; i < tracks.size(); ++i) {
    out << "    { \"user_id\": \"" << tracks[i].user_id << "\", \"display_name\": \"" << tracks[i].display_name << "\" }";
    if (i + 1 < tracks.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"segments\": [\n";
  for (size_t i = 0; i < timeline.entries.size(); ++i) {
    const auto& seg = timeline.entries[i];
    out << "    {\n";
    out << "      \"user_id\": \"" << seg.user_id << "\",\n";
    out << "      \"display_name\": \"" << seg.display_name << "\",\n";
    out << "      \"start_ms\": " << seg.start_ms << ",\n";
    out << "      \"end_ms\": " << seg.end_ms << ",\n";
    out << "      \"confidence\": " << seg.confidence << "\n";
    out << "    }";
    if (i + 1 < timeline.entries.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  return true;
}

bool WriteTimelineText(const std::string& path,
                       const TimelineResult& timeline) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }
  for (const auto& entry : timeline.entries) {
    out << FormatTimestampMs(entry.start_ms) << " - " << FormatTimestampMs(entry.end_ms)
        << "  " << entry.display_name << "\n";
  }
  return true;
}
