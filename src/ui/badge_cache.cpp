#include "ui/badge_cache.h"

#include <SDL3/SDL.h>
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "platform/http_fetch.h"

namespace retro3do {

BadgeCache::~BadgeCache() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    release_textures();
}

std::string BadgeCache::file_for(const std::string& url) const {
    // ".../Badge/12345_lock.png" -> "Badge_12345_lock.png": the folder keeps
    // achievement badges and game icons apart.
    std::string tail = url;
    const size_t scheme = tail.find("://");
    if (scheme != std::string::npos) tail.erase(0, scheme + 3);
    const size_t path = tail.find('/');
    tail = path == std::string::npos ? tail : tail.substr(path + 1);
    for (char& c : tail) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) c = '_';
    }
    return dir_ + "/" + tail;
}

SDL_Texture* BadgeCache::get(const std::string& url) {
    if (url.empty() || dir_.empty()) return nullptr;
    Entry& entry = entries_[url];
    if (entry.texture != nullptr || entry.failed) return entry.texture;
    if (!entry.requested) {
        entry.requested = true;
        {
            std::lock_guard<std::mutex> lock(mu_);
            queue_.push_back(url);
            if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
        }
        cv_.notify_one();
    }
    return nullptr;
}

void BadgeCache::run() {
    for (;;) {
        std::string url;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            url = std::move(queue_.front());
            queue_.pop_front();
        }
        Decoded out;
        out.url = url;
        const std::string path = file_for(url);
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            std::filesystem::create_directories(dir_, error);
            const std::string tmp = path + ".part";
            const std::string failed = http_fetch(url, tmp);
            if (failed.empty()) {
                std::filesystem::rename(tmp, path, error);
            } else {
                std::filesystem::remove(tmp, error);
                SDL_Log("Badge %s: %s", url.c_str(), failed.c_str());
            }
        }
        std::ifstream file(path, std::ios::binary);
        const std::vector<u8> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
        int w = 0, h = 0, channels = 0;
        u8* pixels = bytes.empty() ? nullptr
                                   : stbi_load_from_memory(bytes.data(),
                                                           static_cast<int>(bytes.size()),
                                                           &w, &h, &channels, 4);
        if (pixels != nullptr) {
            out.width = w;
            out.height = h;
            out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
            stbi_image_free(pixels);
        } else {
            // A damaged download is fetched again next time.
            std::filesystem::remove(path, error);
        }
        std::lock_guard<std::mutex> lock(mu_);
        finished_.push_back(std::move(out));
    }
}

void BadgeCache::pump(SDL_Renderer* renderer) {
    std::vector<Decoded> ready;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (finished_.empty()) return;
        ready.swap(finished_);
    }
    for (Decoded& d : ready) {
        const auto found = entries_.find(d.url);
        if (found == entries_.end()) continue;
        Entry& entry = found->second;
        if (d.rgba.empty()) {
            entry.failed = true;
            continue;
        }
        if (renderer == nullptr) {
            entry.requested = false;   // asked for again once there is one
            continue;
        }
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                 SDL_TEXTUREACCESS_STATIC, d.width, d.height);
        if (texture == nullptr) {
            entry.failed = true;
            continue;
        }
        SDL_UpdateTexture(texture, nullptr, d.rgba.data(), d.width * 4);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        entry.texture = texture;
    }
}

void BadgeCache::release_textures() {
    for (auto& [url, entry] : entries_) {
        (void)url;
        if (entry.texture != nullptr) SDL_DestroyTexture(entry.texture);
    }
    entries_.clear();
}

}  // namespace retro3do
