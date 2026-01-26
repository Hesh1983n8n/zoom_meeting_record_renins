#pragma once

#include <cstdint>
#include <fstream>
#include <string>

class WavWriter {
 public:
  bool Open(const std::string& path, int sample_rate, int channels);
  void WriteSamples(const int16_t* samples, size_t count);
  void Close();
  bool IsOpen() const { return file_.is_open(); }
  int SampleRate() const { return sample_rate_; }
  int Channels() const { return channels_; }
  uint64_t TotalSamples() const { return total_samples_; }

 private:
  void WriteHeader();
  void UpdateHeader();

  std::ofstream file_;
  int sample_rate_ = 0;
  int channels_ = 0;
  uint64_t total_samples_ = 0;
};
