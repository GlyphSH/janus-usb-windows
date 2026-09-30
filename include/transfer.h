#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// One committed file plus one staging buffer, mirrored to SD when available.
class Transfer {
 public:
  static constexpr size_t limit = 65536;
  static constexpr size_t chunk_bytes = 256;
  static constexpr size_t max_data_line = 600;
  static constexpr const char* sd_path = "/sd/janus-received.bin";
  std::string command(const std::string& line);
 private:
  uint8_t file_[limit]{};
  uint8_t staging_[limit]{};
  size_t size_ = 0, expected_ = 0, received_ = 0;
  uint32_t expected_crc_ = 0;
  bool receiving_ = false, present_ = false, sd_written_ = false;
  std::string begin(const std::string& args);
  std::string data(const std::string& args);
  std::string read(const std::string& args) const;
  std::string commit();
  std::string sdinfo() const;
  void persist_to_sd();
};
uint32_t crc32(const uint8_t* data, size_t size);
