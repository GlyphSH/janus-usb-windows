#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// One committed file and one staging buffer. No filesystem or host paths.
class Transfer {
 public:
  static constexpr size_t limit = 65536;
  static constexpr size_t chunk_bytes = 256;
  static constexpr size_t max_data_line = 600;
  std::string command(const std::string& line);
 private:
  uint8_t file_[limit]{};
  uint8_t staging_[limit]{};
  size_t size_ = 0, expected_ = 0, received_ = 0;
  uint32_t expected_crc_ = 0;
  bool receiving_ = false, present_ = false;
  std::string begin(const std::string& args);
  std::string data(const std::string& args);
  std::string read(const std::string& args) const;
  std::string commit();
};
uint32_t crc32(const uint8_t* data, size_t size);
