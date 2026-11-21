#include "app/save_slots.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace retro3do {

namespace {

constexpr char kThumbMagic[4] = {'A', '3', 'T', 'H'};
constexpr int kMaxThumbSide = 1024;

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

}  // namespace

std::string save_state_dir(const std::string& data_dir, const std::string& disc_name) {
    std::string name = disc_name;
    // Only a short trailing extension, so "Gex (USA, Europe)" keeps its ")".
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && name.size() - dot <= 5 &&
        name.find_first_of(" ()", dot) == std::string::npos)
        name.erase(dot);
    std::string safe;
    for (const char c : name) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                         c == '"' || c == '<' || c == '>' || c == '|' ||
                         static_cast<unsigned char>(c) < 0x20;
        safe += bad ? '_' : c;
    }
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) safe.pop_back();
    while (!safe.empty() && (safe.front() == ' ' || safe.front() == '.')) safe.erase(0, 1);
    if (safe.empty()) safe = "unnamed";
    return join(join(data_dir, "states"), safe);
}

std::string slot_state_path(const std::string& dir, int slot) {
    return join(dir, "slot" + std::to_string(slot) + ".a3s");
}

std::string slot_thumb_path(const std::string& dir, int slot) {
    return join(dir, "slot" + std::to_string(slot) + ".thumb");
}

Thumbnail make_thumbnail(const u32* xrgb, int width, int height, int max_width) {
    Thumbnail t;
    if (xrgb == nullptr || width <= 0 || height <= 0 || max_width <= 0) return t;
    t.width = std::min(width, max_width);
    t.height = std::max(1, static_cast<int>(static_cast<long long>(height) * t.width / width));
    t.rgba.resize(static_cast<size_t>(t.width) * t.height * 4);
    for (int y = 0; y < t.height; ++y) {
        const int y0 = y * height / t.height;
        const int y1 = std::max(y0 + 1, (y + 1) * height / t.height);
        for (int x = 0; x < t.width; ++x) {
            const int x0 = x * width / t.width;
            const int x1 = std::max(x0 + 1, (x + 1) * width / t.width);
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const u32* row = xrgb + static_cast<size_t>(sy) * width;
                for (int sx = x0; sx < x1; ++sx) {
                    r += (row[sx] >> 16) & 0xff;
                    g += (row[sx] >> 8) & 0xff;
                    b += row[sx] & 0xff;
                    ++n;
                }
            }
            u8* out = &t.rgba[(static_cast<size_t>(y) * t.width + x) * 4];
            out[0] = static_cast<u8>(r / n);
            out[1] = static_cast<u8>(g / n);
            out[2] = static_cast<u8>(b / n);
            out[3] = 255;
        }
    }
    return t;
}

bool write_thumbnail(const std::string& path, const Thumbnail& thumb) {
    if (thumb.width <= 0 || thumb.height <= 0 ||
        thumb.rgba.size() != static_cast<size_t>(thumb.width) * thumb.height * 4)
        return false;
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) return false;
    const u32 size[2] = {static_cast<u32>(thumb.width), static_cast<u32>(thumb.height)};
    bool ok = std::fwrite(kThumbMagic, 1, 4, f) == 4 &&
              std::fwrite(size, sizeof size, 1, f) == 1 &&
              std::fwrite(thumb.rgba.data(), 1, thumb.rgba.size(), f) == thumb.rgba.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool read_thumbnail(const std::string& path, Thumbnail* out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char magic[4];
    u32 size[2] = {};
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, kThumbMagic, 4) == 0 &&
              std::fread(size, sizeof size, 1, f) == 1 && size[0] > 0 && size[1] > 0 &&
              size[0] <= kMaxThumbSide && size[1] <= kMaxThumbSide;
    if (ok) {
        out->width = static_cast<int>(size[0]);
        out->height = static_cast<int>(size[1]);
        out->rgba.resize(static_cast<size_t>(size[0]) * size[1] * 4);
        ok = std::fread(out->rgba.data(), 1, out->rgba.size(), f) == out->rgba.size();
    }
    std::fclose(f);
    return ok;
}

}  // namespace retro3do
