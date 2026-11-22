// RetroAchievements badge images: fetched once into the app's storage,
// decoded off the main thread, uploaded to the GPU a few a frame.
#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "app/types.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace retro3do {

class BadgeCache {
public:
    BadgeCache() = default;
    ~BadgeCache();
    BadgeCache(const BadgeCache&) = delete;
    BadgeCache& operator=(const BadgeCache&) = delete;

    // Where downloaded badges are kept. Call once before get().
    void set_directory(const std::string& dir) { dir_ = dir; }

    // The badge's texture, or null while it is still on its way (or failed).
    SDL_Texture* get(const std::string& url);

    // Upload what the worker has decoded. Once per frame.
    void pump(SDL_Renderer* renderer);
    // Drop every texture (the renderer is going away); files are kept.
    void release_textures();

private:
    struct Decoded {
        std::string url;
        int width = 0, height = 0;
        std::vector<u8> rgba;   // empty when it could not be had
    };
    struct Entry {
        SDL_Texture* texture = nullptr;
        bool requested = false;
        bool failed = false;
    };
    void run();
    std::string file_for(const std::string& url) const;

    std::string dir_;
    std::unordered_map<std::string, Entry> entries_;
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::vector<Decoded> finished_;
    bool stop_ = false;
};

}  // namespace retro3do
