#include "cover_download.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace retro3do {

namespace {

bool safe_image_name(const std::string& name) {
    if (name.empty() || name.size() > 200 || name[0] == '.') return false;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) return false;
    }
    std::string lower;
    for (unsigned char c : name) lower += static_cast<char>(std::tolower(c));
    const auto ends = [&](const char* tail) {
        const std::string t(tail);
        return lower.size() > t.size() &&
               lower.compare(lower.size() - t.size(), t.size(), t) == 0;
    };
    return ends(".jpg") || ends(".jpeg") || ends(".png");
}

long long file_size(const std::string& path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return -1;
    return static_cast<long long>(info.size);
}

}  // namespace

std::vector<CoverPackEntry> CoverDownloader::parse_manifest(const std::string& text) {
    std::vector<CoverPackEntry> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        CoverPackEntry entry;
        entry.name = line.substr(0, tab);
        char* stop = nullptr;
        entry.bytes = std::strtoll(line.c_str() + tab + 1, &stop, 10);
        if (!safe_image_name(entry.name) || entry.bytes <= 0 || *stop != '\0') continue;
        out.push_back(entry);
    }
    return out;
}

CoverDownloader::~CoverDownloader() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

bool CoverDownloader::start(const std::string& base_url, const std::string& directory,
                            Fetch fetch) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.running) return false;
        status_ = Status{};
        status_.running = true;
        status_.message = "Fetching the cover list...";
    }
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    finished_ = false;
    worker_ = std::thread(&CoverDownloader::run, this, base_url, directory, std::move(fetch));
    return true;
}

void CoverDownloader::cancel() { cancel_ = true; }

CoverDownloader::Status CoverDownloader::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool CoverDownloader::take_finished() { return finished_.exchange(false); }

void CoverDownloader::set_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.message = message;
}

void CoverDownloader::run(std::string base_url, std::string directory, Fetch fetch) {
    const auto finish = [&](const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.running = false;
            status_.message = message;
        }
        finished_ = true;
    };
    if (!base_url.empty() && base_url.back() != '/') base_url += '/';
    if (!directory.empty() && directory.back() != '/') directory += '/';
    SDL_CreateDirectory(directory.c_str());

    // The manifest goes beside the covers; it starts with '.', so the
    // folder scan never mistakes it for art.
    const std::string manifest_path = directory + ".covers.txt";
    const std::string error = fetch(base_url + "covers.txt", manifest_path);
    if (!error.empty()) {
        finish("Could not get the cover list: " + error);
        return;
    }
    size_t size = 0;
    char* text = static_cast<char*>(SDL_LoadFile(manifest_path.c_str(), &size));
    const std::vector<CoverPackEntry> entries =
        text != nullptr ? parse_manifest(std::string(text, size)) : std::vector<CoverPackEntry>{};
    SDL_free(text);
    SDL_RemovePath(manifest_path.c_str());
    if (entries.empty()) {
        finish("The cover list was empty or unreadable.");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.total = static_cast<int>(entries.size());
    }

    std::string last_error;
    for (const CoverPackEntry& entry : entries) {
        if (cancel_) break;
        const std::string path = directory + entry.name;
        bool fetched = false, failed = false;
        if (file_size(path) != entry.bytes) {
            set_message("Downloading " + entry.name);
            // Into a side file first, so a half-written image never shows up
            // in the carousel.
            const std::string part = directory + "." + entry.name + ".part";
            std::string why = fetch(base_url + entry.name, part);
            if (why.empty() && file_size(part) != entry.bytes) why = "wrong size";
            if (why.empty() && !SDL_RenamePath(part.c_str(), path.c_str())) why = SDL_GetError();
            if (why.empty()) {
                fetched = true;
            } else {
                SDL_RemovePath(part.c_str());
                last_error = entry.name + ": " + why;
                failed = true;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ++status_.done;
        if (fetched) ++status_.fetched;
        if (failed) ++status_.failed;
    }

    // Covers the pack held last time but no longer does (renamed or
    // dropped) go. Only names the pack itself delivered are on the list, so
    // covers the user put in the folder are never touched.
    const std::string record = directory + ".pack.txt";
    if (!cancel_ && status().failed == 0) {
        std::set<std::string> current;
        for (const CoverPackEntry& entry : entries) current.insert(entry.name);
        size_t old_size = 0;
        char* old_text = static_cast<char*>(SDL_LoadFile(record.c_str(), &old_size));
        if (old_text != nullptr) {
            std::istringstream lines(std::string(old_text, old_size));
            SDL_free(old_text);
            for (std::string name; std::getline(lines, name);) {
                if (safe_image_name(name) && current.count(name) == 0) {
                    SDL_RemovePath((directory + name).c_str());
                }
            }
        }
        std::string text;
        for (const std::string& name : current) text += name + "\n";
        SDL_SaveFile(record.c_str(), text.data(), text.size());
    }

    const Status s = status();
    char summary[256];
    if (cancel_) {
        std::snprintf(summary, sizeof(summary), "Stopped. %d new cover%s downloaded.",
                      s.fetched, s.fetched == 1 ? "" : "s");
    } else if (s.failed > 0) {
        std::snprintf(summary, sizeof(summary),
                      "%d new, %d failed (last: %s). Try again to fetch the rest.",
                      s.fetched, s.failed, last_error.c_str());
    } else if (s.fetched == 0) {
        std::snprintf(summary, sizeof(summary), "All %d covers are up to date.", s.total);
    } else {
        std::snprintf(summary, sizeof(summary), "Done: %d new cover%s, %d in the pack.",
                      s.fetched, s.fetched == 1 ? "" : "s", s.total);
    }
    finish(summary);
}

}  // namespace retro3do
