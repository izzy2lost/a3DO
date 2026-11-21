// Where save states live, and the little picture saved beside each one.
//
// Each game gets its own folder of ten numbered slots:
//
//     <data>/states/<game>/slot3.a3s     the machine (retro3do_save_state)
//     <data>/states/<game>/slot3.thumb   what was on screen when it was saved
//
// The thumbnail is a tiny uncompressed image rather than a PNG: it is written
// once per save, read once per menu visit, and is a few tens of kilobytes, so
// an encoder would be weight for nothing.
#pragma once

#include <string>
#include <vector>

#include "app/types.h"

namespace retro3do {

constexpr int kSaveSlots = 10;

// The folder for a game's slots. The name is the disc's display name, with
// its extension and anything a file system might refuse taken out, so
// "Road Rash (USA).chd" and "Road Rash (USA)" share a folder.
std::string save_state_dir(const std::string& data_dir, const std::string& disc_name);
std::string slot_state_path(const std::string& dir, int slot);   // slot 1..10
std::string slot_thumb_path(const std::string& dir, int slot);

struct Thumbnail {
    int width = 0;
    int height = 0;
    std::vector<u8> rgba;   // width * height * 4, top row first
};

// Shrink an XRGB8888 frame (as the core hands them out) to at most
// `max_width` wide, averaging the pixels each one covers so a busy picture
// stays legible instead of aliasing.
Thumbnail make_thumbnail(const u32* xrgb, int width, int height, int max_width);

bool write_thumbnail(const std::string& path, const Thumbnail& thumb);
bool read_thumbnail(const std::string& path, Thumbnail* out);

}  // namespace retro3do
