// Downloads a cover pack into the app's covers folder.
//
// A pack is a folder of images served over HTTPS beside a manifest,
// covers.txt, with one "name<TAB>bytes" line per image (written by
// tools/covers/make_cover_pack.py). The manifest comes first; then only
// images that are missing, or on disk at a different size, are fetched. So a
// second run after the pack grows downloads just the new covers, and an
// interrupted run picks up where it stopped.
//
// The transfer itself is passed in, so the same code runs over Java's
// HttpURLConnection on Android, curl on the desktop, and a fake in tests.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace retro3do {

struct CoverPackEntry {
    std::string name;
    long long bytes = 0;
};

class CoverDownloader {
public:
    // Fetch `url` into the file `path`. Returns "" on success or a short
    // reason. Called on the downloader's own thread.
    using Fetch = std::function<std::string(const std::string& url,
                                            const std::string& path)>;

    struct Status {
        bool running = false;
        int total = 0;        // images in the pack
        int done = 0;         // checked so far, fetched or already present
        int fetched = 0;      // actually downloaded this run
        int failed = 0;
        std::string message;  // the last thing worth telling the user
    };

    CoverDownloader() = default;
    ~CoverDownloader();
    CoverDownloader(const CoverDownloader&) = delete;
    CoverDownloader& operator=(const CoverDownloader&) = delete;

    // Starts unless a run is already going. `base_url` is the pack folder,
    // with or without a trailing slash.
    bool start(const std::string& base_url, const std::string& directory, Fetch fetch);
    void cancel();
    Status status() const;
    // True once after each run ends, so the caller can rescan the folder.
    bool take_finished();

    // Manifest lines that are safe to act on. Names must be plain image file
    // names: nothing that could climb out of the covers folder.
    static std::vector<CoverPackEntry> parse_manifest(const std::string& text);

private:
    void run(std::string base_url, std::string directory, Fetch fetch);
    void set_message(const std::string& message);

    mutable std::mutex mutex_;
    Status status_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{false};
    std::thread worker_;
};

}  // namespace retro3do
