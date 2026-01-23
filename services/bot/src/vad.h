#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct VadConfig {
  int frame_ms = 20;
  float energy_threshold = 0.015f;
  int min_speech_ms = 200;
  int min_silence_ms = 400;
};

struct VadSegment {
  int64_t start_ms = 0;
  int64_t end_ms = 0;
  float confidence = 0.0f;
};

std::vector<VadSegment> ComputeVadSegments(
    const std::vector<int16_t>& pcm,
    int sample_rate,
    const VadConfig& config);

float ComputeSegmentConfidence(const std::vector<int16_t>& pcm, size_t start_idx, size_t end_idx);

std::string FormatTimestampMs(int64_t ms);
