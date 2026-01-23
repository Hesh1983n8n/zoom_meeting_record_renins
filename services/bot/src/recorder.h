#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mixer.h"
#include "vad.h"
#include "wav_writer.h"

struct RecorderConfig {
  std::string records_dir = "/records";
  bool final_mix_enable = true;
  int final_mix_sample_rate = 48000;
  int final_mix_channels = 1;
  VadConfig vad;
};

struct AudioChunk {
  int64_t start_ms = 0;
  int sample_rate = 0;
  int channels = 0;
  std::vector<int16_t> pcm;
};

struct ParticipantTrack {
  std::string user_id;
  std::string display_name;
  int sample_rate = 0;
  int channels = 0;
  std::vector<AudioChunk> chunks;
  WavWriter writer;
};

enum class RecorderState {
  Idle,
  Authing,
  Joining,
  InMeeting,
  Recording,
  Finalizing,
  Done,
  Error
};

struct RecorderStatus {
  RecorderState state = RecorderState::Idle;
  size_t participants = 0;
  std::string session_id;
  std::optional<std::string> error;
};

class Recorder {
 public:
  explicit Recorder(RecorderConfig config);

  bool StartSession();
  void StopSession();
  void SetState(RecorderState state);

  void OnAudioFrame(const std::string& user_id,
                    const std::string& display_name,
                    int sample_rate,
                    int channels,
                    const int16_t* samples,
                    size_t sample_count,
                    int64_t timestamp_ms);

  RecorderStatus GetStatus() const;

 private:
  std::string GenerateSessionId() const;
  std::string SessionDir() const;
  std::string ParticipantsDir() const;
  std::string FinalDir() const;
  void EnsureDirectories();
  void WriteMetadata(const std::optional<MixResult>& final_mix);
  void AppendEvent(const std::string& line);
  void FinalizeMixAndTimeline();

  RecorderConfig config_;
  RecorderStatus status_{};
  int64_t t0_unix_ms_ = 0;
  std::map<std::string, ParticipantTrack> participants_;
};
