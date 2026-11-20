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

class CoverIndex {
public:
    // `files` are bare file names inside `directory`.
    void rebuild(const std::string& directory,
                 const std::vector<std::string>& files);

    // Full path of the best cover for a library title, or "" when none fits.
    std::string find(const std::string& game_name) const;

    bool empty() const { return by_key_.empty(); }
    size_t size() const { return by_key_.size(); }

    // Lower-case letters and digits only, with region tags, a leading or
    // trailing "the", the gallery's "3do_" prefix and variant suffixes such as
    // "_au" or "--2-" removed. Sets *variant when a suffix was dropped.
    static std::string key(const std::string& name, bool* variant = nullptr);

private:
    std::unordered_map<std::string, std::string> by_key_;   // key -> path
};

// One decoded cover, already split. Back is empty for a front-only image.
struct CoverImage {
    std::string path;
    int front_width = 0, front_height = 0;
    std::vector<u8> front;   // RGBA
    int back_width = 0, back_height = 0;
    std::vector<u8> back;    // RGBA
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
