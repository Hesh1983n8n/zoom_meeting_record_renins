#include "mixer.h"

#include <algorithm>
#include <cmath>

#include "wav_writer.h"

namespace {
std::vector<int16_t> DownmixToMono(const std::vector<int16_t>& pcm, int channels) {
  if (channels <= 1) {
    return pcm;
  }
  size_t frames = pcm.size() / channels;
  std::vector<int16_t> out(frames);
  for (size_t i = 0; i < frames; ++i) {
    int32_t sum = 0;
    for (int c = 0; c < channels; ++c) {
      sum += pcm[i * channels + c];
    }
    out[i] = static_cast<int16_t>(sum / channels);
  }
  return out;
}
}

bool MixTracksToFile(const std::vector<MixTrack>& tracks,
                     const std::string& output_path,
                     const MixSettings& settings,
                     MixResult* result) {
  if (tracks.empty()) {
    return false;
  }

  int sample_rate = settings.sample_rate;
  int channels = settings.channels;
  int64_t max_end_ms = 0;

  struct NormalizedChunk {
    int64_t start_ms;
    std::vector<int16_t> pcm;
  };

  std::vector<NormalizedChunk> normalized;
  for (const auto& track : tracks) {
    for (const auto& chunk : track.chunks) {
      if (chunk.sample_rate <= 0 || chunk.pcm.empty()) {
        continue;
      }
      if (chunk.sample_rate != sample_rate) {
        // TODO: resampling. For now, skip mismatched sample rates.
        continue;
      }
      std::vector<int16_t> pcm = chunk.pcm;
      if (chunk.channels != channels) {
        pcm = DownmixToMono(chunk.pcm, chunk.channels);
      }
      int64_t duration_ms = static_cast<int64_t>(
          (static_cast<double>(pcm.size()) / sample_rate) * 1000.0);
      max_end_ms = std::max(max_end_ms, chunk.start_ms + duration_ms);
      normalized.push_back({chunk.start_ms, std::move(pcm)});
    }
  }

  if (max_end_ms <= 0) {
    return false;
  }

  size_t total_samples = static_cast<size_t>((max_end_ms * sample_rate) / 1000);
  std::vector<float> mix(total_samples, 0.0f);

  for (const auto& chunk : normalized) {
    size_t start_idx = static_cast<size_t>((chunk.start_ms * sample_rate) / 1000);
    for (size_t i = 0; i < chunk.pcm.size() && start_idx + i < mix.size(); ++i) {
      mix[start_idx + i] += static_cast<float>(chunk.pcm[i]) / 32768.0f;
    }
  }

  float max_abs = 0.0f;
  for (float v : mix) {
    max_abs = std::max(max_abs, std::abs(v));
  }
  float gain = max_abs > 1.0f ? 1.0f / max_abs : 1.0f;

  std::vector<int16_t> out_pcm(mix.size());
  for (size_t i = 0; i < mix.size(); ++i) {
    float v = mix[i] * gain;
    v = std::max(-1.0f, std::min(1.0f, v));
    out_pcm[i] = static_cast<int16_t>(v * 32767.0f);
  }

  WavWriter writer;
  if (!writer.Open(output_path, sample_rate, channels)) {
    return false;
  }
  writer.WriteSamples(out_pcm.data(), out_pcm.size());
  writer.Close();

  if (result) {
    result->sample_rate = sample_rate;
    result->channels = channels;
    result->duration_ms = max_end_ms;
  }
  return true;
}
