#include "ui/cover_art.h"
#include "test_harness.h"

#include <vector>

using namespace retro3do;

TEST(cover_keys_ignore_punctuation_regions_and_variants) {
    bool variant = true;
    CHECK(CoverIndex::key("Road Rash (USA).chd", &variant) == "roadrash");
    CHECK(!variant);
    CHECK(CoverIndex::key("Road-Rash.jpg") == "roadrash");
    CHECK(CoverIndex::key("3do_wayofthewarrior.jpg") == "wayofthewarrior");
    CHECK(CoverIndex::key("3do_nighttrap_au.jpg", &variant) == "nighttrap");
    CHECK(variant);
    CHECK(CoverIndex::key("Braindead-13--2-.jpg", &variant) == "braindead13");
    CHECK(variant);
    CHECK(CoverIndex::key("Braindead-13.jpg", &variant) == "braindead13");
    CHECK(!variant);
    CHECK(CoverIndex::key("3do_themepark_2.jpg", &variant) == "themepark");
    CHECK(variant);
    CHECK(CoverIndex::key("Horde--The.jpg") == "horde");
    CHECK(CoverIndex::key("The Horde (USA)") == "horde");
}

TEST(cover_index_prefers_plain_names_and_finds_titles_inside_longer_ones) {
    CoverIndex index;
    index.rebuild("/covers", {"Gex--2-.jpg", "Gex.jpg", "3do_nighttrap_au.jpg",
                              "Need-for-Speed--The.jpg", "Doom.jpg",
                              "notes.txt", "Super-Streetfighter-II-Turbo.jpg"});
    CHECK(index.find("Gex (USA)") == "/covers/Gex.jpg");
    CHECK(index.find("Night Trap (USA)") == "/covers/3do_nighttrap_au.jpg");
    CHECK(index.find("Road & Track Presents - The Need for Speed (USA, Europe)") ==
          "/covers/Need-for-Speed--The.jpg");
    CHECK(index.find("Super Street Fighter II Turbo (USA)") ==
          "/covers/Super-Streetfighter-II-Turbo.jpg");
    CHECK(index.find("3D Atlas (USA)").empty());
    CHECK_EQ(index.size(), size_t{5});
}
