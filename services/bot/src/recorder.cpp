#include "recorder.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

#include "timeline.h"
#include "util_sanitize.h"

namespace fs = std::filesystem;

Recorder::Recorder(RecorderConfig config) : config_(std::move(config)) {
  status_.state = RecorderState::Idle;
}

bool Recorder::StartSession() {
  status_.session_id = GenerateSessionId();
  status_.state = RecorderState::Recording;
  status_.participants = 0;
  status_.error.reset();

  t0_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  EnsureDirectories();
  WriteMetadata(std::nullopt);
  AppendEvent("session_started");
  return true;
}

void Recorder::StopSession() {
  if (status_.state == RecorderState::Idle || status_.state == RecorderState::Finalizing) {
    return;
  }
  status_.state = RecorderState::Finalizing;

  for (auto& [_, track] : participants_) {
    track.writer.Close();
  }

  FinalizeMixAndTimeline();
  AppendEvent("session_stopped");
  status_.state = RecorderState::Done;
}

void Recorder::SetState(RecorderState state) {
  status_.state = state;
}

void Recorder::OnAudioFrame(const std::string& user_id,
                            const std::string& display_name,
                            int sample_rate,
                            int channels,
                            const int16_t* samples,
                            size_t sample_count,
                            int64_t timestamp_ms) {
  if (status_.state != RecorderState::Recording) {
    return;
  }
  auto& track = participants_[user_id];
  if (track.user_id.empty()) {
    track.user_id = user_id;
    track.display_name = display_name;
    track.sample_rate = sample_rate;
    track.channels = channels;

    std::string safe_name = SanitizeFilename(display_name);
    std::string filename = user_id + "_" + safe_name + ".wav";
    std::string path = ParticipantsDir() + "/" + filename;
    track.writer.Open(path, sample_rate, channels);
  }

  track.writer.WriteSamples(samples, sample_count);
  AudioChunk chunk;
  chunk.start_ms = timestamp_ms;
  chunk.sample_rate = sample_rate;
  chunk.channels = channels;
  chunk.pcm.assign(samples, samples + sample_count);
  track.chunks.push_back(std::move(chunk));
  status_.participants = participants_.size();
}

RecorderStatus Recorder::GetStatus() const {
  return status_;
}

std::string Recorder::GenerateSessionId() const {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::mt19937 rng(static_cast<uint32_t>(now_ms));
  std::uniform_int_distribution<int> dist(0, 0xffffff);
  std::string suffix = std::to_string(dist(rng));
  return std::to_string(now_ms) + "_" + suffix;
}

std::string Recorder::SessionDir() const {
  return config_.records_dir + "/" + status_.session_id;
}

std::string Recorder::ParticipantsDir() const {
  return SessionDir() + "/participants";
}

std::string Recorder::FinalDir() const {
  return SessionDir() + "/final";
}

void Recorder::EnsureDirectories() {
  fs::create_directories(ParticipantsDir());
  fs::create_directories(FinalDir());
}

void Recorder::WriteMetadata(const std::optional<MixResult>& final_mix) {
  std::ofstream out(SessionDir() + "/metadata.json");
  if (!out.is_open()) {
    return;
  }
  out << "{\n";
  out << "  \"session_id\": \"" << status_.session_id << "\",\n";
  out << "  \"t0_unix_ms\": " << t0_unix_ms_;
  if (final_mix.has_value()) {
    out << ",\n";
    out << "  \"final_mix\": \"final/final_mix.wav\",\n";
    out << "  \"speaker_timeline\": \"final/speaker_timeline.json\"\n";
  } else {
    out << "\n";
  }
  out << "}\n";
}

void Recorder::AppendEvent(const std::string& line) {
  std::ofstream out(SessionDir() + "/events.log", std::ios::app);
  if (out.is_open()) {
    out << line << "\n";
  }
}

void Recorder::FinalizeMixAndTimeline() {
  if (!config_.final_mix_enable) {
    return;
  }
  std::vector<MixTrack> tracks;
  tracks.reserve(participants_.size());
  for (const auto& [_, track] : participants_) {
    MixTrack mix_track;
    mix_track.user_id = track.user_id;
    mix_track.display_name = track.display_name;
    for (const auto& chunk : track.chunks) {
      MixChunk mix_chunk;
      mix_chunk.start_ms = chunk.start_ms;
      mix_chunk.sample_rate = chunk.sample_rate;
      mix_chunk.channels = chunk.channels;
      mix_chunk.pcm = chunk.pcm;
      mix_track.chunks.push_back(std::move(mix_chunk));
    }
    tracks.push_back(std::move(mix_track));
  }

  MixSettings settings;
  settings.sample_rate = config_.final_mix_sample_rate;
  settings.channels = config_.final_mix_channels;

  MixResult mix_result;
  std::string final_path = FinalDir() + "/final_mix.wav";
  if (!MixTracksToFile(tracks, final_path, settings, &mix_result)) {
    status_.error = "final mix failed";
    status_.state = RecorderState::Error;
    return;
  }

  TimelineResult timeline = BuildTimeline(tracks, settings.sample_rate, config_.vad);
  WriteTimelineJson(FinalDir() + "/speaker_timeline.json", status_.session_id, t0_unix_ms_, mix_result, tracks, timeline);
  WriteTimelineText(FinalDir() + "/speaker_timeline.txt", timeline);
  WriteMetadata(mix_result);
}
