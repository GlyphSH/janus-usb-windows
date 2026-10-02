#include "transfer.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#ifdef ESP_PLATFORM
#include "event_log.h"
#endif

namespace {

// IEEE 802.3 / zlib / PNG CRC-32 constants. Reflected polynomial form:
// bits enter the LSB and the final XOR inverts the running register.
constexpr uint32_t kCrc32Polynomial = 0xEDB88320u;
constexpr uint32_t kCrc32Init = 0xFFFFFFFFu;

constexpr std::size_t kMaxHexCharsPerChunk = Transfer::chunk_bytes * 2;
constexpr std::size_t kDecodedChunkCapacity = Transfer::chunk_bytes;
static_assert(kDecodedChunkCapacity == 256,
              "decoded buffer must match protocol chunk capacity");

// RAII wrapper for a C FILE*. std::fclose is noexcept; wrapping its
// signature in the deleter type keeps construction/destruction visible
// at the call site without a bespoke guard class. Only the firmware
// build touches real files, so the helper is marked maybe_unused for
// the host test TU.
using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

[[maybe_unused]] FilePtr open_file(const char* path, const char* mode) {
    return FilePtr{std::fopen(path, mode), std::fclose};
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes `hex` into `out`. Caller sized `out` to at least `hex.size()/2`.
// Returns false on any non-hex character; `out` may be partially written.
bool decode_hex(std::string_view hex, uint8_t* out) {
    const std::size_t pairs = hex.size() / 2;
    for (std::size_t i = 0; i < pairs; ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// Parses "<size_t> <uint32>" and refuses trailing junk. The wire protocol
// uses decimal integers; istringstream matches that without pulling in
// locale machinery.
bool parse_size_and_u32(const std::string& args,
                        std::size_t& first, uint32_t& second) {
    std::istringstream in{args};
    std::string trailing;
    return bool(in >> first >> second) && !(in >> trailing);
}

}  // namespace

uint32_t crc32(const uint8_t* data, std::size_t size) {
    uint32_t crc = kCrc32Init;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ kCrc32Polynomial;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

std::string Transfer::begin(const std::string& args) {
    std::size_t length = 0;
    uint32_t checksum = 0;
    if (!parse_size_and_u32(args, length, checksum) || length > limit) {
        return "ERR SIZE";
    }
    // A second BEGIN mid-transfer deliberately discards any staged bytes;
    // the host restarts from offset 0 against the new size/CRC.
    expected_ = length;
    expected_crc_ = checksum;
    received_ = 0;
    receiving_ = true;
    return "OK BEGIN";
}

std::string Transfer::data(const std::string& args) {
    std::istringstream in{args};
    std::size_t offset = 0;
    std::string hex;
    std::string trailing;
    if (!(in >> offset >> hex) || (in >> trailing)
        || hex.size() % 2 != 0 || hex.size() > kMaxHexCharsPerChunk) {
        return "ERR DATA";
    }
    if (!receiving_) return "ERR STATE";
    const std::size_t bytes = hex.size() / 2;
    const std::size_t remaining = expected_ - received_;
    if (offset != received_ || bytes > remaining) return "ERR OFFSET";
    uint8_t decoded[kDecodedChunkCapacity];
    if (!decode_hex(hex, decoded)) return "ERR HEX";
    std::memcpy(staging_ + received_, decoded, bytes);
    received_ += bytes;
    return "OK DATA " + std::to_string(received_);
}

std::string Transfer::commit() {
    if (!receiving_) return "ERR STATE";
    if (received_ != expected_) return "ERR INCOMPLETE";
    // Leave DATA-accepting mode before verifying the CRC: a failed commit
    // must not permit further DATA against a half-trusted staging buffer.
    receiving_ = false;
    if (crc32(staging_, expected_) != expected_crc_) return "ERR CRC";
    std::memcpy(file_, staging_, expected_);
    size_ = expected_;
    present_ = true;
    persist_to_sd();
    return "OK COMMIT";
}

void Transfer::persist_to_sd() {
    sd_written_ = false;
#ifdef ESP_PLATFORM
    // Write-then-rename so a crash or short write can never leave a
    // half-overwritten janus-received.bin on the card. On FatFs this is
    // not a true atomic swap (remove + rename has a brief window where
    // neither name exists), but it does guarantee the committed name
    // never points at a torn file.
    static constexpr const char* tmp_path = "/sd/janus-received.bin.tmp";
    auto f = open_file(tmp_path, "wb");
    if (!f) return;
    const bool wrote = std::fwrite(file_, 1, size_, f.get()) == size_
                       && std::fflush(f.get()) == 0;
    f.reset();  // fclose before rename
    if (!wrote) {
        ::remove(tmp_path);
        return;
    }
    ::remove(sd_path);  // FatFs rename refuses to replace an existing file
    if (::rename(tmp_path, sd_path) != 0) {
        ::remove(tmp_path);
        return;
    }
    sd_written_ = true;
#endif
}

std::string Transfer::sdinfo() const {
    if (!sd_written_) return "ERR SD";
    return std::string{"SD "} + sd_path + " " + std::to_string(size_);
}

std::string Transfer::sddiag() const {
#ifdef ESP_PLATFORM
    if (!sd_mounted()) {
        return std::string{"DIAG NO-MOUNT esp_err="} +
               std::to_string(sd_last_mount_error());
    }
    auto f = open_file("/sd/janus-probe", "wb");
    if (!f) {
        return std::string{"DIAG NO-WRITE errno="} + std::to_string(errno);
    }
    const int c = std::fputc('x', f.get());
    f.reset();  // close before remove()
    ::remove("/sd/janus-probe");
    if (c == EOF) return "DIAG WRITE-FAIL";
    return "DIAG OK";
#else
    return "DIAG UNSUPPORTED";
#endif
}

std::string Transfer::read(const std::string& args) const {
    if (!present_) return "ERR EMPTY";
    std::size_t offset = 0;
    uint32_t count = 0;
    if (!parse_size_and_u32(args, offset, count)
        || count > chunk_bytes
        || offset > size_
        || count > size_ - offset) {
        return "ERR RANGE";
    }
    static constexpr char hex_digits[] = "0123456789abcdef";
    std::string out = "DATA ";
    out.reserve(out.size() + count * 2);
    for (std::size_t i = offset; i < offset + count; ++i) {
        out += hex_digits[file_[i] >> 4];
        out += hex_digits[file_[i] & 0x0F];
    }
    return out;
}

std::string Transfer::command(const std::string& line) {
    const auto split = line.find(' ');
    const std::string verb = line.substr(0, split);
    const std::string args = split == std::string::npos
        ? std::string{}
        : line.substr(split + 1);

    // Verbs that take arguments.
    if (verb == "BEGIN") return begin(args);
    if (verb == "DATA")  return data(args);
    if (verb == "READ")  return read(args);

    // Verbs that take no arguments; refuse trailing junk.
    if (!args.empty()) return "ERR COMMAND";
    if (verb == "HELLO")  return "JANUS 1 65536";
    if (verb == "COMMIT") return commit();
    if (verb == "ABORT")  { receiving_ = false; return "OK ABORT"; }
    if (verb == "INFO") {
        return present_
            ? "FILE " + std::to_string(size_) + " "
                      + std::to_string(crc32(file_, size_))
            : "ERR EMPTY";
    }
    if (verb == "SDINFO") return sdinfo();
    if (verb == "SDDIAG") return sddiag();
    return "ERR COMMAND";
}
