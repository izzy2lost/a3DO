// A 3DO disc's identity for RetroAchievements.
//
// The same recipe as rcheevos' rc_hash_3do: MD5 over the Opera volume header
// (the first 132 bytes of sector 0) followed by the whole of the root
// directory's LaunchMe, the program the BIOS boots. It is done here over the
// core's own sector reader rather than rcheevos' CD-image readers, so CHD,
// BIN/CUE, ISO and Android file descriptors all hash exactly as they play.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace retro3do::cheevos {

// Reads `count` 2048-byte user sectors from `lba` into `out`.
using SectorReader = std::function<bool(uint32_t lba, uint32_t count, uint8_t* out)>;

// 32 lowercase hex digits in `out`, or false with the reason in `err`.
bool hash_3do_disc(const SectorReader& read, std::string& out, std::string& err);

}  // namespace retro3do::cheevos
