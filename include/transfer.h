#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Line-oriented ASCII protocol handler for the Janus USB file transfer.
//
// Transfer owns one committed file and one staging buffer. A single
// command()/response round-trip advances a small state machine:
//
//   BEGIN size crc  : staging_ cleared, receiving_ = true, received_ = 0
//                     (ERR SIZE if size > limit). A second BEGIN mid-
//                     transfer discards partial staging rather than
//                     silently resuming at the old offset.
//   DATA offset hex : appends to staging_. ERR STATE if no BEGIN is in
//                     progress; ERR OFFSET if offset != received_ or the
//                     payload exceeds the remaining budget; ERR DATA on a
//                     malformed argument; ERR HEX on a non-hex nibble.
//   ABORT           : clears receiving_ (ERR never returned). staging_
//                     and file_ untouched; subsequent DATA/COMMIT return
//                     ERR STATE until the next BEGIN.
//   COMMIT          : ERR STATE if no BEGIN is in progress; ERR INCOMPLETE
//                     if fewer bytes staged than announced. On CRC match,
//                     copies staging_ into file_ and marks it present;
//                     on CRC mismatch, clears receiving_ and leaves file_
//                     unchanged (ERR CRC).
//   READ offset n   : hex-encodes n bytes from file_ (ERR EMPTY if no
//                     file is present, ERR RANGE on bounds).
//   INFO            : "FILE size crc" of file_, or ERR EMPTY.
//   SDINFO          : last SD mirror status.
//   SDDIAG          : SD mount/write diagnostic probe.
//
// All ERR responses are plain prefixes; the exact strings are the wire
// protocol and are pinned by tests/transfer_test.cpp. Do not change a
// response string without updating that test.
//
// Design note (alternatives considered):
//   The class owns a 64 KiB staging buffer and a 64 KiB committed buffer
//   as members, 128 KiB total out of the ESP32-S3's ~512 KiB SRAM. The
//   win is atomicity: a mid-transfer failure never corrupts the already-
//   committed file. The alternative we rejected is streaming DATA chunks
//   straight to an SD journal with rename-on-commit, which halves RAM
//   but makes correctness depend on SD presence and FatFs ordering. For
//   a bench tool that mirrors to SD best-effort and must work with no
//   card inserted, the RAM-resident design is the simpler safe choice.
class Transfer {
public:
    // Protocol-visible limits. Shared with main.cpp's line buffer and with
    // the host-side janus.ps1 / janus.py transports.
    static constexpr std::size_t limit = 65536;        // max committed file size
    static constexpr std::size_t chunk_bytes = 256;    // max bytes per DATA chunk
    static constexpr std::size_t max_data_line = 600;  // guard for the line buffer
    static constexpr const char* sd_path = "/sd/janus-received.bin";

    // Processes one complete command line and returns the response.
    // `line` must not include the trailing newline. Non-reentrant;
    // the firmware calls this from a single polling task.
    std::string command(const std::string& line);

private:
    // 64 KiB committed file plus 64 KiB staging. The ESP32-S3 build
    // declares exactly one global Transfer, so the 128 KiB lives in BSS,
    // not on any task stack.
    uint8_t file_[limit]{};
    uint8_t staging_[limit]{};

    // Running state. See the state-machine comment above.
    std::size_t size_ = 0;         // bytes committed to file_
    std::size_t expected_ = 0;     // size announced by the current BEGIN
    std::size_t received_ = 0;     // bytes staged since the current BEGIN
    uint32_t expected_crc_ = 0;    // CRC32 announced by the current BEGIN
    bool receiving_ = false;       // between BEGIN and COMMIT/ABORT/BEGIN
    bool present_ = false;         // at least one COMMIT has succeeded
    bool sd_written_ = false;      // last COMMIT also wrote the SD mirror

    std::string begin(const std::string& args);
    std::string data(const std::string& args);
    std::string read(const std::string& args) const;
    std::string commit();
    std::string sdinfo() const;
    std::string sddiag() const;
    void persist_to_sd();
};

// CRC-32/ISO-HDLC (zlib/PNG family): polynomial 0xEDB88320 reflected,
// init 0xFFFFFFFF, final XOR 0xFFFFFFFF. Exposed as a free function
// because janus.ps1 and transfer_test.cpp both verify against it.
uint32_t crc32(const uint8_t* data, std::size_t size);
