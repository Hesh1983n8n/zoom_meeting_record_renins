#include "vad.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

std::vector<VadSegment> ComputeVadSegments(
    const std::vector<int16_t>& pcm,
    int sample_rate,
    const VadConfig& config) {
  std::vector<VadSegment> segments;
  if (pcm.empty() || sample_rate <= 0) {
    return segments;
  }

  int frame_samples = (sample_rate * config.frame_ms) / 1000;
  if (frame_samples <= 0) {
    return segments;
  }

  bool in_speech = false;
  int64_t speech_start_ms = 0;
  int64_t last_speech_ms = 0;

  for (size_t i = 0; i + frame_samples <= pcm.size(); i += frame_samples) {
    float energy = 0.0f;
    for (size_t j = i; j < i + frame_samples; ++j) {
      float sample = static_cast<float>(pcm[j]) / 32768.0f;
      energy += sample * sample;
    }
    energy = std::sqrt(energy / frame_samples);
    int64_t frame_ms = static_cast<int64_t>((static_cast<double>(i) / sample_rate) * 1000.0);

    if (energy > config.energy_threshold) {
      if (!in_speech) {
        speech_start_ms = frame_ms;
        in_speech = true;
      }
      last_speech_ms = frame_ms + config.frame_ms;
    } else if (in_speech) {
      int64_t silence_ms = frame_ms - last_speech_ms;
      if (silence_ms >= config.min_silence_ms) {
        int64_t speech_end = last_speech_ms;
        if (speech_end - speech_start_ms >= config.min_speech_ms) {
          segments.push_back({speech_start_ms, speech_end, 0.0f});
        }
        in_speech = false;
      }
    }
  }

  if (in_speech) {
    int64_t end_ms = static_cast<int64_t>((static_cast<double>(pcm.size()) / sample_rate) * 1000.0);
    if (end_ms - speech_start_ms >= config.min_speech_ms) {
      segments.push_back({speech_start_ms, end_ms, 0.0f});
    }
  }

  return segments;
}

float ComputeSegmentConfidence(const std::vector<int16_t>& pcm, size_t start_idx, size_t end_idx) {
  if (start_idx >= end_idx || end_idx > pcm.size()) {
    return 0.0f;
  }
  double energy = 0.0;
  size_t count = end_idx - start_idx;
  for (size_t i = start_idx; i < end_idx; ++i) {
    double sample = static_cast<double>(pcm[i]) / 32768.0;
    energy += sample * sample;
  }
  double rms = std::sqrt(energy / count);
  return static_cast<float>(std::min(1.0, rms * 2.0));
}

std::string FormatTimestampMs(int64_t ms) {
  int64_t total_seconds = ms / 1000;
  int64_t milli = ms % 1000;
  int64_t minutes = total_seconds / 60;
  int64_t seconds = total_seconds % 60;
  int64_t hours = minutes / 60;
  minutes %= 60;

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << hours << ":"
      << std::setw(2) << minutes << ":"
      << std::setw(2) << seconds << "."
      << std::setw(3) << milli;
  return oss.str();
}
