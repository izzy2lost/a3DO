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
    CoverRegion region = CoverRegion::us;
    CHECK(CoverIndex::key("3do_nighttrap_au.jpg", &variant, &region) == "nighttrap");
    CHECK(!variant);
    CHECK(region == CoverRegion::eu);
    CHECK(CoverIndex::key("dragonslair_jp.jpg", &variant, &region) == "dragonslair");
    CHECK(region == CoverRegion::jp);
    CHECK(CoverIndex::key("3do_dragonslair_au_2.jpg", &variant, &region) == "dragonslair");
    CHECK(variant);
    CHECK(region == CoverRegion::eu);
    CHECK(CoverIndex::key("gex.jpg", &variant, &region) == "gex");
    CHECK(region == CoverRegion::us);
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

TEST(cover_index_picks_the_games_own_region) {
    CoverIndex index;
    index.rebuild("/c", {"nighttrap.jpg", "nighttrap_eu.jpg", "nighttrap_jp.jpg",
                         "totaleclipse_jp.jpg", "doom_eu.jpg", "thehorde.jpg",
                         "lastbountyhunterthe.jpg"});
    CHECK(index.find("Night Trap (USA)") == "/c/nighttrap.jpg");
    CHECK(index.find("Night Trap (Europe)") == "/c/nighttrap_eu.jpg");
    CHECK(index.find("Night Trap (Japan)") == "/c/nighttrap_jp.jpg");
    CHECK(index.find("Night Trap") == "/c/nighttrap.jpg");
    CHECK(index.find("Night Trap (USA, Europe)") == "/c/nighttrap.jpg");
    // Only one region on file: use it whatever the game's region.
    CHECK(index.find("Total Eclipse (USA)") == "/c/totaleclipse_jp.jpg");
    CHECK(index.find("Doom (USA)") == "/c/doom_eu.jpg");
    // "The" glued to either end of the gallery's name.
    CHECK(index.find("Horde, The (USA)") == "/c/thehorde.jpg");
    CHECK(index.find("The Last Bounty Hunter (USA)") == "/c/lastbountyhunterthe.jpg");
    CHECK(CoverIndex::title_region("Doctor Hauzer (Japan)") == CoverRegion::jp);
    CHECK(CoverIndex::title_region("FIFA Soccer (Germany)") == CoverRegion::eu);
}

TEST(cover_index_reads_numerals_and_short_disc_names) {
    CHECK(CoverIndex::key("Star Control II (USA, Europe)") == "starcontrol2");
    CHECK(CoverIndex::key("Mad Dog III") == "maddog3");
    CHECK(CoverIndex::key("Ivory Tower") == "ivorytower");   // only whole words
    CoverIndex index;
    // A pack named after Redump titles.
    index.rebuild("/c", {"starcontrol2.jpg", "twistedthegameshow.jpg", "familyfeud.jpg",
                         "roadandtrackpresentstheneedforspeed.jpg"});
    CHECK(index.find("Star Control 2.iso") == "/c/starcontrol2.jpg");
    CHECK(index.find("Family Feud (USA).chd") == "/c/familyfeud.jpg");
    CHECK(index.find("Twisted.iso") == "/c/twistedthegameshow.jpg");
    CHECK(index.find("Need for Speed.cue") == "/c/roadandtrackpresentstheneedforspeed.jpg");
    CHECK(index.find("Gex.iso").empty());   // too short to search inside names
}
