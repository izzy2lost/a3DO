// Box art for the cover carousel.
//
// Covers are full wraps - back, spine and front side by side, as printed for a
// DVD-style case - kept as ordinary JPEG or PNG files in one folder. Nothing is
// bundled with the app: the user supplies the images, and a rescan copies any
// it finds in a "covers" folder beside their games into private storage.
//
// Two halves live here. CoverIndex is pure string matching from a library
// title to an image file, so it is unit tested. CoverDecoder decodes and splits
// images on a worker thread, so a swipe through the carousel never waits on a
// JPEG.
#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "app/types.h"

namespace retro3do {

// Which release a cover or a game belongs to. PAL covers count as Europe.
enum class CoverRegion { us = 0, eu = 1, jp = 2 };

class CoverIndex {
public:
    // `files` are bare file names inside `directory`.
    void rebuild(const std::string& directory,
                 const std::vector<std::string>& files);

    // Full path of the best cover for a library title, or "" when none fits.
    // A Japanese or European release gets its own region's cover when there
    // is one, and the US cover otherwise.
    std::string find(const std::string& game_name) const;

    bool empty() const { return by_key_.empty(); }
    size_t size() const { return by_key_.size(); }

    // Lower-case letters and digits only, with region tags, a leading or
    // trailing "the", the gallery's "3do_" prefix and suffixes such as "_jp",
    // "_au" or "--2-" removed. Sets *variant when a numbered suffix was
    // dropped, and *region from a region suffix (US when there is none).
    static std::string key(const std::string& name, bool* variant = nullptr,
                           CoverRegion* region = nullptr);

    // The region a library title's tags name: "(Japan)" is Japanese,
    // "(Europe)" or a European country is PAL, anything else is US.
    static CoverRegion title_region(const std::string& game_name);

private:
    struct Entry {
        std::string path[3];     // indexed by CoverRegion
        bool variant[3] = {false, false, false};
    };
    const Entry* lookup(const std::string& key) const;

    std::unordered_map<std::string, Entry> by_key_;
};

// One decoded cover, already split. Back and spine are empty for a
// front-only image.
struct CoverImage {
    std::string path;
    int front_width = 0, front_height = 0;
    std::vector<u8> front;   // RGBA
    int back_width = 0, back_height = 0;
    std::vector<u8> back;    // RGBA
    int spine_width = 0, spine_height = 0;
    std::vector<u8> spine;   // RGBA, empty when the image has no spine
    // Share of the front's width, from its left edge, taken by the US
    // "Interactive Multiplayer" banner. The library leaves it out so only
    // the cover art shows and US boxes match the others' shape.
    float front_banner = 0.0f;
    bool failed = false;
};

class CoverDecoder {
public:
    // Panels are scaled so neither side exceeds this height.
    explicit CoverDecoder(int max_height = 560);
    ~CoverDecoder();

    CoverDecoder(const CoverDecoder&) = delete;
    CoverDecoder& operator=(const CoverDecoder&) = delete;

    // Queue a file. The newest request is decoded first, so the cover the user
    // has just scrolled to wins over ones that have already gone past.
    void request(const std::string& path);
    // Forget everything still queued (not finished results).
    void cancel_pending();

    // Finished images, oldest first. Called from the UI thread.
    std::vector<CoverImage> take_finished();

    // Split and scale an encoded image. Public for tests.
    static CoverImage decode(const std::string& path, const u8* bytes,
                             size_t size, int max_height);

private:
    void run();

    const int max_height_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> queue_;
    std::vector<CoverImage> finished_;
    bool stop_ = false;
    std::thread worker_;
};

}  // namespace retro3do
