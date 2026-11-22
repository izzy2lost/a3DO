#include "cheevos/cheevos_hash.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

extern "C" {
#include "md5.h"
}

namespace retro3do::cheevos {

namespace {

constexpr uint32_t kSector = 2048;
// A LaunchMe is a program loaded into two megabytes of RAM; anything claiming
// to be bigger is a damaged directory, not a game.
constexpr size_t kMaxLaunchMe = 8u << 20;
// Directory blocks to follow before giving up on a corrupt chain.
constexpr int kMaxDirectoryBlocks = 256;

uint32_t be24(const uint8_t* p) { return (uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | p[2]; }
uint32_t be16(const uint8_t* p) { return (uint32_t{p[0]} << 8) | p[1]; }

// The entry's name field is 32 bytes, NUL-padded.
bool named_launchme(const uint8_t* name) {
    static const char kWant[] = "launchme";
    for (size_t i = 0; i < 32; ++i) {
        const char c = static_cast<char>(name[i]);
        if (i == sizeof kWant - 1) return c == '\0';
        if (std::tolower(static_cast<unsigned char>(c)) != kWant[i]) return false;
    }
    return false;
}

}  // namespace

bool hash_3do_disc(const SectorReader& read, std::string& out, std::string& err) {
    out.clear();
    err.clear();
    uint8_t buffer[kSector];
    if (!read(0, 1, buffer)) {
        err = "could not read the disc";
        return false;
    }
    static const uint8_t kOperaFs[7] = {0x01, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x01};
    if (std::memcmp(buffer, kOperaFs, sizeof kOperaFs) != 0) {
        err = "not a 3DO disc";
        return false;
    }

    md5_state_t md5;
    md5_init(&md5);
    md5_append(&md5, buffer, 132);

    uint32_t block_size = be24(&buffer[0x4D]);
    const uint32_t root = be24(&buffer[0x65]) * block_size;   // bytes
    if (block_size == 0) {
        err = "the disc's volume header is damaged";
        return false;
    }

    uint32_t sector = root / kSector;
    uint32_t file_start = 0;
    size_t size = 0;
    for (int blocks = 0; blocks < kMaxDirectoryBlocks; ++blocks) {
        if (!read(sector, 1, buffer)) {
            err = "could not read the disc's directory";
            return false;
        }
        uint32_t offset = be16(&buffer[0x12]);
        const uint32_t stop = std::min<uint32_t>(be24(&buffer[0x0D]), kSector);
        while (offset < stop && offset + 0x48 <= kSector) {
            const uint8_t* entry = &buffer[offset];
            if (entry[0x03] == 0x02 && named_launchme(&entry[0x20])) {
                const uint32_t file_block = be24(&entry[0x0D]);
                file_start = be24(&entry[0x45]) * file_block;
                size = be24(&entry[0x11]);
                break;
            }
            offset += 0x48 + entry[0x43] * 4u;
        }
        if (size != 0) break;

        // The directory may carry on in another block.
        const uint32_t next = be16(&buffer[0x02]);
        if (next == 0xFFFF) break;
        sector = (root + next * block_size) / kSector;
    }
    if (size == 0) {
        err = "the disc has no LaunchMe";
        return false;
    }
    if (size > kMaxLaunchMe) {
        err = "the disc's LaunchMe is implausibly large";
        return false;
    }

    // Read in runs: each call may park the emulator, so ask for a lot at once.
    std::vector<uint8_t> run(64 * kSector);
    sector = file_start / kSector;
    while (size > 0) {
        const uint32_t sectors = static_cast<uint32_t>(
            std::min<size_t>(64, (size + kSector - 1) / kSector));
        if (!read(sector, sectors, run.data())) {
            err = "could not read the disc's LaunchMe";
            return false;
        }
        const size_t take = std::min<size_t>(size, size_t{sectors} * kSector);
        md5_append(&md5, run.data(), static_cast<int>(take));
        size -= take;
        sector += sectors;
    }

    uint8_t digest[16];
    md5_finish(&md5, digest);
    static const char kHex[] = "0123456789abcdef";
    out.reserve(32);
    for (const uint8_t b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return true;
}

}  // namespace retro3do::cheevos
