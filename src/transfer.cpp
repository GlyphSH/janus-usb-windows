#include "transfer.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#ifdef ESP_PLATFORM
#include "event_log.h"
#endif

static constexpr size_t decoded_chunk_capacity = Transfer::chunk_bytes;
static_assert(decoded_chunk_capacity == 256, "decoded buffer must match protocol chunk capacity");

uint32_t crc32(const uint8_t* data, size_t size) {
 uint32_t crc = 0xffffffffu;
 for (size_t i=0; i<size; ++i) {
  crc ^= data[i];
  for (int bit=0; bit<8; ++bit) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
 }
 return ~crc;
}
static bool numbers(const std::string& text, size_t& first, uint32_t& second) {
 std::istringstream in(text); std::string extra;
 return bool(in >> first >> second) && !(in >> extra);
}
static int nibble(char c) {
 if(c>='0' && c<='9') return c-'0';
 if(c>='a' && c<='f') return c-'a'+10;
 if(c>='A' && c<='F') return c-'A'+10;
 return -1;
}
std::string Transfer::begin(const std::string& args) {
 size_t length; uint32_t checksum;
 if(!numbers(args,length,checksum) || length>limit) return "ERR SIZE";
 expected_=length; expected_crc_=checksum; received_=0; receiving_=true;
 return "OK BEGIN";
}
std::string Transfer::data(const std::string& args) {
 std::istringstream in(args); size_t offset; std::string hex,extra;
 if(!(in>>offset>>hex) || (in>>extra) || hex.size()%2 || hex.size()>512) return "ERR DATA";
 if(!receiving_ || offset!=received_ || hex.size()/2>expected_-received_) return "ERR OFFSET";
 uint8_t decoded[decoded_chunk_capacity];
 for(size_t i=0;i<hex.size()/2;++i) {
  int high=nibble(hex[2*i]),low=nibble(hex[2*i+1]);
  if(high<0 || low<0) return "ERR HEX";
  decoded[i]=static_cast<uint8_t>((high<<4)|low);
 }
 std::memcpy(staging_+received_,decoded,hex.size()/2); received_+=hex.size()/2;
 return "OK DATA " + std::to_string(received_);
}
std::string Transfer::commit() {
 if(!receiving_ || received_!=expected_) return "ERR INCOMPLETE";
 receiving_=false;
 if(crc32(staging_,expected_)!=expected_crc_) return "ERR CRC";
 std::memcpy(file_,staging_,expected_); size_=expected_; present_=true;
 persist_to_sd();
 return "OK COMMIT";
}
void Transfer::persist_to_sd() {
 sd_written_=false;
#ifdef ESP_PLATFORM
 FILE* f=std::fopen(sd_path,"wb");
 if(!f) return;
 sd_written_=(std::fwrite(file_,1,size_,f)==size_);
 std::fclose(f);
#endif
}
std::string Transfer::sdinfo() const {
 if(!sd_written_) return "ERR SD";
 return std::string("SD ")+sd_path+" "+std::to_string(size_);
}
std::string Transfer::sddiag() const {
#ifdef ESP_PLATFORM
 if(!sd_mounted()) return std::string("DIAG NO-MOUNT esp_err=")+std::to_string(sd_last_mount_error());
 FILE* f=std::fopen("/sd/janus-probe","wb");
 if(!f) return std::string("DIAG NO-WRITE errno=")+std::to_string(errno);
 int c=std::fputc('x',f);
 std::fclose(f);
 ::remove("/sd/janus-probe");
 if(c==EOF) return "DIAG WRITE-FAIL";
 return "DIAG OK";
#else
 return "DIAG UNSUPPORTED";
#endif
}
std::string Transfer::read(const std::string& args) const {
 size_t offset; uint32_t count;
 if(!present_) return "ERR EMPTY";
 if(!numbers(args,offset,count) || count>256 || offset>size_ || count>size_-offset) return "ERR RANGE";
 const char* digits="0123456789abcdef";
 std::string out="DATA ";
 for(size_t i=offset;i<offset+count;++i){out+=digits[file_[i]>>4];out+=digits[file_[i]&15];}
 return out;
}
std::string Transfer::command(const std::string& line) {
 const auto split=line.find(' '); const auto verb=line.substr(0,split);
 const auto args=split==std::string::npos ? "" : line.substr(split+1);
 if(verb=="BEGIN") return begin(args);
 if(verb=="DATA") return data(args);
 if(verb=="READ") return read(args);
 if(!args.empty()) return "ERR COMMAND";
 if(verb=="HELLO") return "JANUS 1 65536";
 if(verb=="COMMIT") return commit();
 if(verb=="ABORT"){receiving_=false;return "OK ABORT";}
 if(verb=="INFO") return present_ ? "FILE "+std::to_string(size_)+" "+std::to_string(crc32(file_,size_)) : "ERR EMPTY";
 if(verb=="SDINFO") return sdinfo();
 if(verb=="SDDIAG") return sddiag();
 return "ERR COMMAND";
}
