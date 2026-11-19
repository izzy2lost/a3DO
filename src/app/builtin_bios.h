// The built-in system ROM: opera-boot, an open-source replacement for the
// retail 3DO boot ROM (third_party/opera-boot). It initialises the machine,
// loads the operating system mastered on the disc and hands over to it, so a
// user can play without supplying a BIOS of their own. A real BIOS can still be
// chosen in Settings, and replaces this one when it is.
//
// a3DO's copy of opera-boot replaces the upstream startup logo with original
// artwork. The bytes are generated at configure time from
// third_party/opera-boot/prebuilt/opera-boot.bin.
#pragma once

#include <cstddef>

namespace retro3do {

extern const unsigned char kOperaBootRom[];
extern const std::size_t kOperaBootRom_size;

constexpr const char* kBuiltinBiosName = "opera-boot (built-in)";

}  // namespace retro3do
