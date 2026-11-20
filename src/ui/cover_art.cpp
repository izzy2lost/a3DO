#include "cover_art.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace retro3do {

namespace {

std::string lowercase(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool ends_with(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size() &&
           text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

bool is_image_name(const std::string& name) {
    const std::string lower = lowercase(name);
    return ends_with(lower, ".jpg") || ends_with(lower, ".jpeg") ||
           ends_with(lower, ".png");
}

}  // namespace

std::string CoverIndex::key(const std::string& name, bool* variant) {
    std::string text = lowercase(name);
    if (variant != nullptr) *variant = false;

    // Extension.
    const size_t dot = text.rfind('.');
    if (dot != std::string::npos && dot + 5 >= text.size() &&
        dot + 1 < text.size() &&
        std::isalpha(static_cast<unsigned char>(text[dot + 1]))) {
        text.erase(dot);
    }

    // Region and revision tags: "(USA)", "[!]".
    std::string stripped;
    int depth = 0;
    for (char c : text) {
        if (c == '(' || c == '[') { ++depth; continue; }
        if ((c == ')' || c == ']') && depth > 0) { --depth; continue; }
        if (depth == 0) stripped += c;
    }
    text = stripped;
    while (!text.empty() && text.back() == ' ') text.pop_back();

    if (text.rfind("3do_", 0) == 0) text.erase(0, 4);

    // Variant suffixes: "_au", "_2", "--2-".
    const auto drop = [&](size_t n) {
        text.erase(text.size() - n);
        if (variant != nullptr) *variant = true;
    };
    for (;;) {
        const size_t end = text.size();
        const auto digit = [&](size_t i) {
            return std::isdigit(static_cast<unsigned char>(text[i])) != 0;
        };
        if (ends_with(text, "_au")) { drop(3); continue; }
        if (end >= 4 && text.compare(end - 4, 2, "--") == 0 && digit(end - 2) &&
            text[end - 1] == '-') { drop(4); continue; }
        if (end >= 2 && text[end - 2] == '_' && digit(end - 1)) { drop(2); continue; }
        break;
    }

    // "The" as a whole word at either end: "The Horde", "Horde, The",
    // "Horde--The". Not the start of "Theme Park".
    const auto separator = [](char c) {
        return c == ' ' || c == '-' || c == '_' || c == ',';
    };
    if (text.size() > 4 && text.compare(0, 3, "the") == 0 && separator(text[3])) {
        text.erase(0, 4);
    }
    if (text.size() > 4 && ends_with(text, "the") &&
        separator(text[text.size() - 4])) {
        text.erase(text.size() - 4);
    }

    // Letters and digits only, with "&" read as "and".
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c)) out += static_cast<char>(c);
        else if (c == '&') out += "and";
    }
    return out;
}

void CoverIndex::rebuild(const std::string& directory,
                         const std::vector<std::string>& files) {
    by_key_.clear();
    // Plain names beat variants; among equals the first name alphabetically
    // wins, so the result does not depend on directory order.
    std::vector<std::string> sorted = files;
    std::sort(sorted.begin(), sorted.end());
    std::unordered_map<std::string, bool> is_variant;
    for (const std::string& file : sorted) {
        if (!is_image_name(file)) continue;
        bool variant = false;
        const std::string k = key(file, &variant);
        if (k.size() < 3) continue;
        const auto found = is_variant.find(k);
        if (found != is_variant.end() && (variant || !found->second)) continue;
        is_variant[k] = variant;
        std::string path = directory;
        if (!path.empty() && path.back() != '/') path += '/';
        by_key_[k] = path + file;
    }
}

std::string CoverIndex::find(const std::string& game_name) const {
    const std::string k = key(game_name);
    if (k.empty()) return {};
    const auto exact = by_key_.find(k);
    if (exact != by_key_.end()) return exact->second;

    // "Road & Track Presents - The Need for Speed" holds "needforspeed".
    // Prefer the longest cover name the title contains, so "Doom II" does not
    // settle for "Doom" when both exist.
    std::string best_path;
    size_t best = 0;
    for (const auto& [cover, path] : by_key_) {
        if (cover.size() < 5 || cover.size() <= best) continue;
        if (k.find(cover) != std::string::npos) {
            best = cover.size();
            best_path = path;
        }
    }
    return best_path;
}

// ---------------------------------------------------------------------------

namespace {

// Box-filter `src` (sw x sh, RGBA, row stride `stride` pixels) down to dw x dh.
std::vector<u8> resample(const u8* src, int stride, int sw, int sh, int dw,
                         int dh) {
    std::vector<u8> out(static_cast<size_t>(dw) * dh * 4);
    const double fx = static_cast<double>(sw) / dw;
    const double fy = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const int y0 = static_cast<int>(y * fy);
        const int y1 = std::max(y0 + 1, std::min(sh, static_cast<int>((y + 1) * fy)));
        for (int x = 0; x < dw; ++x) {
            const int x0 = static_cast<int>(x * fx);
            const int x1 = std::max(x0 + 1, std::min(sw, static_cast<int>((x + 1) * fx)));
            u32 sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                const u8* row = src + (static_cast<size_t>(sy) * stride + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4) {
                    sum[0] += row[0]; sum[1] += row[1];
                    sum[2] += row[2]; sum[3] += row[3];
                }
            }
            const u32 n = static_cast<u32>((y1 - y0) * (x1 - x0));
            u8* dst = &out[(static_cast<size_t>(y) * dw + x) * 4];
            for (int c = 0; c < 4; ++c) dst[c] = static_cast<u8>(sum[c] / n);
        }
    }
    return out;
}

}  // namespace

CoverImage CoverDecoder::decode(const std::string& path, const u8* bytes,
                                size_t size, int max_height) {
    CoverImage image;
    image.path = path;
    int w = 0, h = 0, channels = 0;
    u8* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h,
                                       &channels, 4);
    if (pixels == nullptr || w < 8 || h < 8) {
        if (pixels != nullptr) stbi_image_free(pixels);
        image.failed = true;
        return image;
    }

    // A wrap is wider than tall: back on the left, then the spine, then the
    // front. Where the spine starts and ends differs between print templates
    // (a US "3DO REAL" spine is about an eighth of the width; a PAL or DVD
    // case spine is a twentieth), so look for the long vertical edges that
    // bound it rather than assuming one layout.
    const bool wrap = w * 10 >= h * 12;
    int back_end = w / 2, front_start = w / 2;
    if (wrap) {
        // For each column, the share of rows with a sharp colour step to the
        // next column. A printed panel edge runs the full height; picture
        // detail does not.
        std::vector<float> edge(static_cast<size_t>(w), 0.0f);
        for (int y = 0; y < h; y += 2) {
            const u8* row = pixels + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x + 1 < w; ++x) {
                const u8* p = row + x * 4;
                const int step = std::abs(p[0] - p[4]) + std::abs(p[1] - p[5]) +
                                 std::abs(p[2] - p[6]);
                if (step > 60) edge[static_cast<size_t>(x)] += 1.0f;
            }
        }
        const float rows = static_cast<float>((h + 1) / 2);
        const auto strongest = [&](double from, double to, float* score) {
            const int a = static_cast<int>(from * w), b = static_cast<int>(to * w);
            int best = a;
            for (int x = a; x < b && x < w; ++x) {
                if (edge[static_cast<size_t>(x)] > edge[static_cast<size_t>(best)]) best = x;
            }
            *score = edge[static_cast<size_t>(best)] / rows;
            return best + 1;
        };
        const double aspect = static_cast<double>(w) / h;
        if (aspect > 1.3 && aspect < 1.7) {
            float back_score = 0.0f, narrow_score = 0.0f, wide_score = 0.0f;
            back_end = strongest(0.44, 0.50, &back_score);
            const int narrow = strongest(0.515, 0.545, &narrow_score);
            const int wide = strongest(0.58, 0.66, &wide_score);
            front_start = wide_score > std::max(narrow_score, 0.3f) ? wide : narrow;
            if (back_score < 0.2f) back_end = std::min(back_end, w - front_start);
        }
    }
    const int front_w = wrap ? w - front_start : w;
    const int out_h = std::min(h, max_height);
    const auto scaled = [&](int width) {
        return std::max(1, static_cast<int>(std::lround(
                               static_cast<double>(width) * out_h / h)));
    };

    image.front_width = scaled(front_w);
    image.front_height = out_h;
    image.front = resample(pixels + static_cast<size_t>(wrap ? front_start : 0) * 4,
                           w, front_w, h, image.front_width, out_h);
    if (wrap) {
        image.back_width = scaled(back_end);
        image.back_height = out_h;
        image.back = resample(pixels, w, back_end, h, image.back_width, out_h);
        // The spine is drawn on the side of the box, so its width also
        // sets how deep the box is. Trim a column either side so the panel
        // edges do not bleed onto it.
        const int spine_from = back_end + 1, spine_w = front_start - back_end - 2;
        if (spine_w >= 2) {
            image.spine_width = scaled(spine_w);
            image.spine_height = out_h;
            image.spine = resample(pixels + static_cast<size_t>(spine_from) * 4, w,
                                   spine_w, h, image.spine_width, out_h);
        }
    }
    stbi_image_free(pixels);
    return image;
}

CoverDecoder::CoverDecoder(int max_height)
    : max_height_(max_height), worker_([this] { run(); }) {}

CoverDecoder::~CoverDecoder() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CoverDecoder::request(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto queued = std::find(queue_.begin(), queue_.end(), path);
        if (queued != queue_.end()) queue_.erase(queued);
        queue_.push_back(path);
    }
    wake_.notify_one();
}

void CoverDecoder::cancel_pending() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

std::vector<CoverImage> CoverDecoder::take_finished() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CoverImage> out;
    out.swap(finished_);
    return out;
}

void CoverDecoder::run() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            path = queue_.back();
            queue_.pop_back();
        }
        size_t size = 0;
        void* file = SDL_LoadFile(path.c_str(), &size);
        CoverImage image;
        if (file != nullptr) {
            image = decode(path, static_cast<const u8*>(file), size, max_height_);
            SDL_free(file);
        } else {
            image.path = path;
            image.failed = true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        finished_.push_back(std::move(image));
    }
}

}  // namespace retro3do
