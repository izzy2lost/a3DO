// The RetroAchievements session, over rcheevos' rc_client.
//
// It signs in, loads the set for the disc in the drive, checks it after every
// emulated frame and reports what happens as notices for the UI. Casual
// (softcore) mode only: a3DO has save states and is not an approved hardcore
// client.
//
// Two threads use it. The core's frame hook calls on_frame() on the emulator
// thread; everything else is called from the main loop. rc_client is not
// thread-safe, so every call into it is made holding `lock_`. Requests to
// the server run on a thread of their own, and their replies are handed to
// rc_client under the same lock the next time either thread comes by.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "retro3do.h"

namespace retro3do::cheevos {

// Something the player should see, as a pop-up.
struct Notice {
    enum class Type { Earned, News, Trouble };
    Type type = Type::News;
    std::string title;
    std::string body;
    unsigned points = 0;       // an earned achievement's worth
    std::string image;         // its badge, or the game's icon
};

enum class Status {
    Unavailable,     // no way to reach the server on this device
    SignedOut,
    SigningIn,
    Ready,           // signed in, no set loaded
    Loading,
    Active,          // a set is loaded and being checked
    UnknownDisc,     // the server does not know this disc
    NoAchievements,  // a known game with no published set yet
};

// What a RetroAchievements 3DO address holds.
//
// The sets were made on the Opera core, which stores each 32-bit word of RAM
// in the host's (little-endian) byte order. This core keeps RAM as the
// hardware does, big-endian, so the byte at Opera address A is the byte at
// A ^ 3 here. Returns how many bytes fell inside the two megabytes of RAM;
// the others read as zero.
uint32_t read_opera_ram(retro3do_t* emu, uint32_t address, uint8_t* out, uint32_t count);

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Bring rc_client up. False, with the reason in why_unavailable(), when
    // this device cannot reach the server.
    bool open(retro3do_t* emu);
    void close();

    Status status() const;
    const std::string& why_unavailable() const { return unavailable_; }

    void sign_in(const std::string& user, const std::string& password);
    void resume_sign_in(const std::string& user, const std::string& token);
    void sign_out();
    std::string display_name() const;
    std::string account_name() const;   // what resume_sign_in wants back
    std::string token() const;
    unsigned points() const;

    // The disc in the drive changed. `hash` comes from hash_3do_disc. A new
    // game unloads the old set; a swap to another disc of the same game
    // keeps it.
    void insert_disc(const std::string& hash, bool same_game);
    void remove_disc();
    std::string game_name() const;
    std::string game_icon() const;

    void on_frame();               // emulator thread, after each frame
    void pump(bool game_running);  // main loop, every frame
    void on_machine_reset();

    // Achievement progress for a save state. A blob that does not fit the
    // loaded set is refused, and the caller should on_machine_reset() rather
    // than carry on with progress from somewhere else.
    std::vector<uint8_t> save_progress() const;
    bool restore_progress(const std::vector<uint8_t>& blob);

    std::vector<Notice> take_notices();

    struct Entry {
        std::string name, description, progress, badge, badge_locked;
        unsigned points = 0;
        float percent = 0.0f;
        bool earned = false;
        bool unsupported = false;
    };
    std::vector<Entry> entries() const;

    // rcheevos calls back through these; not for anyone else.
    struct Callbacks;
    friend struct Callbacks;

private:
    void notify(Notice::Type type, std::string title, std::string body = {},
                unsigned points = 0, std::string image = {});
    void run_network();
    void deliver_replies();
    void start_loading();
    std::vector<Entry> entries_locked() const;

    retro3do_t* emu_ = nullptr;
    void* rc_ = nullptr;                    // rc_client_t*
    mutable std::recursive_mutex lock_;     // around every rc_client call
    Status status_ = Status::Unavailable;
    std::string unavailable_;
    std::string agent_;
    std::string disc_hash_;
    bool waiting_for_sign_in_ = false;      // a disc is in; load it once signed in

    // Network: requests out, replies back. Guarded by queue_lock_.
    std::thread network_;
    std::mutex queue_lock_;
    std::condition_variable queue_wake_;
    std::deque<std::function<void()>> outgoing_;
    std::deque<std::function<void()>> replies_;
    bool closing_ = false;

    std::mutex notices_lock_;
    std::vector<Notice> notices_;
};

// The sign-in kept between runs: the account and its token, never the
// password. `dir` is the app's private data folder.
struct SavedLogin {
    std::string user;
    std::string token;
    bool empty() const { return user.empty() || token.empty(); }
};
bool read_saved_login(const std::string& dir, SavedLogin& out);
bool write_saved_login(const std::string& dir, const SavedLogin& login);
void forget_saved_login(const std::string& dir);

}  // namespace retro3do::cheevos
