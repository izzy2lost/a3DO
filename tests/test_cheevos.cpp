#include "cheevos/cheevos_client.h"
#include "cheevos/cheevos_hash.h"
#include "test_harness.h"

#include <cstring>
#include <filesystem>
#include <vector>

extern "C" {
#include "md5.h"
}

using namespace retro3do;
using namespace retro3do::cheevos;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSector = 2048;

void put24(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 16);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v);
}

// Adds a directory entry at `offset` and returns the next one's offset.
uint32_t add_entry(uint8_t* dir, uint32_t offset, const char* name, uint32_t block,
                   uint32_t size, uint32_t copies = 0) {
    uint8_t* e = dir + offset;
    e[0x03] = 0x02;                 // a file
    put24(&e[0x0D], kSector);       // its block size
    put24(&e[0x11], size);
    std::strncpy(reinterpret_cast<char*>(&e[0x20]), name, 32);
    e[0x43] = static_cast<uint8_t>(copies);
    put24(&e[0x45], block);
    return offset + 0x48 + copies * 4;
}

// A small Opera volume: header in sector 0, the root directory in sectors 1
// (and 2, when `split`), LaunchMe from sector 4.
struct Disc {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(12 * kSector, 0);
    std::vector<uint8_t> program;

    explicit Disc(bool split, const char* launch_name = "LaunchMe") {
        uint8_t* header = bytes.data();
        const uint8_t id[7] = {0x01, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x01};
        std::memcpy(header, id, 7);
        std::memcpy(header + 0x28, "TESTDISC", 8);
        put24(&header[0x4D], kSector);
        put24(&header[0x65], 1);

        uint8_t* dir = bytes.data() + kSector;
        dir[0x12] = 0;
        dir[0x13] = 0x14;
        uint32_t end = add_entry(dir, 0x14, "AppStartup", 9, 100, 1);
        if (split) {
            dir[0x02] = 0;
            dir[0x03] = 1;    // continued one block on
            put24(&dir[0x0D], end);
            dir = bytes.data() + 2 * kSector;
            dir[0x02] = 0xFF;
            dir[0x03] = 0xFF;
            dir[0x13] = 0x14;
            end = 0x14;
        } else {
            dir[0x02] = 0xFF;
            dir[0x03] = 0xFF;
        }
        end = add_entry(dir, end, launch_name, 4, 5000);
        put24(&dir[0x0D], end);

        program.resize(5000);
        for (size_t i = 0; i < program.size(); ++i) program[i] = static_cast<uint8_t>(i * 7 + 3);
        std::memcpy(bytes.data() + 4 * kSector, program.data(), program.size());
        // Bytes after the file's end must not reach the hash.
        std::memset(bytes.data() + 4 * kSector + program.size(), 0xEE, 100);
    }

    SectorReader reader() const {
        return [this](uint32_t lba, uint32_t count, uint8_t* out) {
            if ((lba + count) * kSector > bytes.size()) return false;
            std::memcpy(out, bytes.data() + lba * kSector, count * kSector);
            return true;
        };
    }

    std::string expected() const {
        md5_state_t md5;
        md5_init(&md5);
        md5_append(&md5, bytes.data(), 132);
        md5_append(&md5, program.data(), static_cast<int>(program.size()));
        uint8_t digest[16];
        md5_finish(&md5, digest);
        char hex[33];
        for (int i = 0; i < 16; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
        return hex;
    }
};

}  // namespace

TEST(a_3do_disc_hashes_its_volume_header_and_launchme) {
    const Disc disc(false);
    std::string hash, err;
    CHECK(hash_3do_disc(disc.reader(), hash, err));
    CHECK_EQ(hash.size(), size_t{32});
    CHECK(hash == disc.expected());
}

TEST(the_launchme_is_found_in_a_directory_continued_in_another_block) {
    const Disc disc(true, "LAUNCHME");
    std::string hash, err;
    CHECK(hash_3do_disc(disc.reader(), hash, err));
    CHECK(hash == disc.expected());
}

TEST(a_disc_without_opera_or_launchme_is_refused_with_a_reason) {
    Disc no_launch(false, "LaunchMeNot");
    std::string hash, err;
    CHECK(!hash_3do_disc(no_launch.reader(), hash, err));
    CHECK(!err.empty());

    Disc not_3do(false);
    not_3do.bytes[1] = 0;
    CHECK(!hash_3do_disc(not_3do.reader(), hash, err));
    CHECK(err == "not a 3DO disc");
}

TEST(opera_addresses_reach_ram_with_each_word_byte_swapped) {
    retro3do_t* emu = retro3do_create();
    retro3do_load_demo(emu);
    for (int i = 0; i < 30; ++i) retro3do_run_frame(emu);

    // Find a word that is not palindromic so the order shows.
    uint32_t at = 0;
    uint8_t be[4] = {};
    for (; at < 0x200000; at += 4) {
        retro3do_read_ram(emu, at, be, 4);
        if (be[0] != be[3] || be[1] != be[2]) break;
    }
    CHECK(at < 0x200000u);
    uint8_t opera[4] = {};
    CHECK_EQ(read_opera_ram(emu, at, opera, 4), 4u);
    for (int i = 0; i < 4; ++i) CHECK_EQ(int(opera[i]), int(be[3 - i]));

    // An unaligned byte, and a run straddling the end of RAM.
    uint8_t one = 0;
    read_opera_ram(emu, at + 1, &one, 1);
    CHECK_EQ(int(one), int(be[2]));
    uint8_t tail[8];
    CHECK_EQ(read_opera_ram(emu, 0x1FFFFC, tail, 8), 4u);
    CHECK_EQ(int(tail[7]), 0);
    retro3do_destroy(emu);
}

TEST(a_sign_in_is_kept_as_name_and_token_and_can_be_forgotten) {
    const fs::path dir = fs::temp_directory_path() / "a3do_cheevos_test";
    fs::create_directories(dir);
    const SavedLogin saved{"player", "abc123"};
    CHECK(write_saved_login(dir.string(), saved));
    SavedLogin back;
    CHECK(read_saved_login(dir.string(), back));
    CHECK(back.user == "player");
    CHECK(back.token == "abc123");
    forget_saved_login(dir.string());
    CHECK(!read_saved_login(dir.string(), back));
    CHECK(back.empty());
    fs::remove_all(dir);
}
