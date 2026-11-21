#include "app/save_slots.h"
#include "test_harness.h"

#include <filesystem>
#include <vector>

using namespace retro3do;
namespace fs = std::filesystem;

TEST(save_state_folders_are_named_after_the_game_and_safe) {
    CHECK(save_state_dir("/data", "Road Rash (USA).chd") == "/data/states/Road Rash (USA)");
    CHECK(save_state_dir("/data/", "Road Rash (USA)") == "/data/states/Road Rash (USA)");
    CHECK(save_state_dir("/d", "Gex (USA, Europe)") == "/d/states/Gex (USA, Europe)");
    CHECK(save_state_dir("/d", "a/b:c?.iso") == "/d/states/a_b_c_");
    CHECK(save_state_dir("/d", "...") == "/d/states/unnamed");
    CHECK(slot_state_path("/d/states/Gex", 3) == "/d/states/Gex/slot3.a3s");
    CHECK(slot_thumb_path("/d/states/Gex", 10) == "/d/states/Gex/slot10.thumb");
}

TEST(a_thumbnail_averages_and_survives_a_round_trip) {
    // 4x2: left half red, right half blue.
    const std::vector<u32> frame = {0xff0000, 0xff0000, 0x0000ff, 0x0000ff,
                                    0xff0000, 0xff0000, 0x0000ff, 0x0000ff};
    const Thumbnail t = make_thumbnail(frame.data(), 4, 2, 2);
    CHECK_EQ(t.width, 2);
    CHECK_EQ(t.height, 1);
    CHECK_EQ(int(t.rgba[0]), 255);
    CHECK_EQ(int(t.rgba[2]), 0);
    CHECK_EQ(int(t.rgba[4]), 0);
    CHECK_EQ(int(t.rgba[6]), 255);

    const fs::path path = fs::temp_directory_path() / "a3do_thumb_test.thumb";
    CHECK(write_thumbnail(path.string(), t));
    Thumbnail back;
    CHECK(read_thumbnail(path.string(), &back));
    CHECK_EQ(back.width, 2);
    CHECK(back.rgba == t.rgba);
    fs::resize_file(path, 9);
    CHECK(!read_thumbnail(path.string(), &back));
    fs::remove(path);
}
