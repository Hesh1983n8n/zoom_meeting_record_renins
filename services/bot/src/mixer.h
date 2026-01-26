#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MixChunk {
  int64_t start_ms = 0;
  int sample_rate = 0;
  int channels = 0;
  std::vector<int16_t> pcm;
};

struct MixTrack {
  std::string user_id;
  std::string display_name;
  std::vector<MixChunk> chunks;
};

struct MixSettings {
  int sample_rate = 48000;
  int channels = 1;
};

struct MixResult {
  int sample_rate = 48000;
  int channels = 1;
  int64_t duration_ms = 0;
};

bool MixTracksToFile(const std::vector<MixTrack>& tracks,
                     const std::string& output_path,
                     const MixSettings& settings,
                     MixResult* result);
