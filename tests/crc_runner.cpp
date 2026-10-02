// Hex-in, hex-out CRC32 CLI wrapper around the firmware's crc32().
// Used by tests/crc_cross_check.py to prove the C++ implementation agrees
// with Python's zlib.crc32 on a shared corpus.
#include "transfer.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <hex>\n", argv[0]);
        return 2;
    }
    const std::size_t len = std::strlen(argv[1]);
    if (len % 2 != 0) {
        std::fprintf(stderr, "hex length must be even\n");
        return 2;
    }
    std::vector<uint8_t> data(len / 2);
    for (std::size_t i = 0; i < data.size(); ++i) {
        const int hi = nibble(argv[1][2 * i]);
        const int lo = nibble(argv[1][2 * i + 1]);
        if (hi < 0 || lo < 0) {
            std::fprintf(stderr, "non-hex nibble at position %zu\n", 2 * i);
            return 2;
        }
        data[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    std::printf("%08x\n", crc32(data.data(), data.size()));
    return 0;
}
