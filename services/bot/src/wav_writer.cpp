#include "wav_writer.h"

#include <cstring>

bool WavWriter::Open(const std::string& path, int sample_rate, int channels) {
  sample_rate_ = sample_rate;
  channels_ = channels;
  total_samples_ = 0;
  file_.open(path, std::ios::binary | std::ios::out);
  if (!file_.is_open()) {
    return false;
  }
  WriteHeader();
  return true;
}

void WavWriter::WriteSamples(const int16_t* samples, size_t count) {
  if (!file_.is_open() || count == 0) {
    return;
  }
  file_.write(reinterpret_cast<const char*>(samples), static_cast<std::streamsize>(count * sizeof(int16_t)));
  total_samples_ += count;
}

void WavWriter::Close() {
  if (!file_.is_open()) {
    return;
  }
  UpdateHeader();
  file_.close();
}

void WavWriter::WriteHeader() {
  uint32_t byte_rate = sample_rate_ * channels_ * sizeof(int16_t);
  uint16_t block_align = channels_ * sizeof(int16_t);
  uint32_t data_size = 0;
  uint32_t chunk_size = 36 + data_size;

  file_.write("RIFF", 4);
  file_.write(reinterpret_cast<const char*>(&chunk_size), 4);
  file_.write("WAVE", 4);
  file_.write("fmt ", 4);

  uint32_t subchunk1_size = 16;
  uint16_t audio_format = 1;
  file_.write(reinterpret_cast<const char*>(&subchunk1_size), 4);
  file_.write(reinterpret_cast<const char*>(&audio_format), 2);
  file_.write(reinterpret_cast<const char*>(&channels_), 2);
  file_.write(reinterpret_cast<const char*>(&sample_rate_), 4);
  file_.write(reinterpret_cast<const char*>(&byte_rate), 4);
  file_.write(reinterpret_cast<const char*>(&block_align), 2);
  uint16_t bits_per_sample = 16;
  file_.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

  file_.write("data", 4);
  file_.write(reinterpret_cast<const char*>(&data_size), 4);
}

void WavWriter::UpdateHeader() {
  uint32_t data_size = static_cast<uint32_t>(total_samples_ * sizeof(int16_t));
  uint32_t chunk_size = 36 + data_size;

  file_.seekp(4, std::ios::beg);
  file_.write(reinterpret_cast<const char*>(&chunk_size), 4);
  file_.seekp(40, std::ios::beg);
  file_.write(reinterpret_cast<const char*>(&data_size), 4);
  file_.seekp(0, std::ios::end);
}
