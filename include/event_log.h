#pragma once

#include <cstdint>
#include <string>

class EventLog {
 public:
  bool begin();
  void record(const char *event, const char *detail = "");
  void flush();
  void tick();

 private:
  bool ready_ = false;
  std::string pending_;
  uint32_t last_flush_ms_ = 0;
  uint8_t pending_events_ = 0;

  uint32_t now_ms() const;
  bool append_file(const char *path, const std::string &data);
};
