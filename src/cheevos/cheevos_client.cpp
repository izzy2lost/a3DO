#include "cheevos/cheevos_client.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "cheevos/cheevos_http.h"
#include "rc_client.h"
#include "rc_version.h"

#ifndef A3DO_CHEEVOS_VERSION
#define A3DO_CHEEVOS_VERSION "2.0"
#endif

namespace retro3do::cheevos {

namespace {

constexpr uint32_t kRamSize = 0x200000;
constexpr const char* kLoginFile = "/retroachievements.login";

// The server sends "warning" pseudo-achievements (an emulator it does not
// fully trust, say) with ids from here up. They are not the player's to earn.
bool is_pseudo_achievement(uint32_t id) { return id >= 101000001u; }

rc_client_t* rc(void* p) { return static_cast<rc_client_t*>(p); }

}  // namespace

uint32_t read_opera_ram(retro3do_t* emu, uint32_t address, uint8_t* out, uint32_t count) {
    uint32_t inside = 0;
    uint8_t word[4];
    uint32_t word_at = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t opera = uint64_t{address} + i;
        if (opera >= kRamSize || emu == nullptr) {
            out[i] = 0;
            continue;
        }
        const uint32_t base = static_cast<uint32_t>(opera) & ~3u;
        if (base != word_at) {
            retro3do_read_ram(emu, base, word, 4);
            word_at = base;
        }
        out[i] = word[(opera & 3u) ^ 3u];
        ++inside;
    }
    return inside;
}

// ---------------------------------------------------------------------------
// What rc_client calls back into. Every one of these runs inside an
// rc_client call, so with lock_ already held.

struct Session::Callbacks {
    static Session* of(const rc_client_t* client) {
        return static_cast<Session*>(rc_client_get_userdata(client));
    }

    static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t count,
                                rc_client_t* client) {
        Session* s = of(client);
        return read_opera_ram(s != nullptr ? s->emu_ : nullptr, address, buffer, count);
    }

    // Queue the request; its reply comes back through deliver_replies().
    static void send(const rc_api_request_t* request, rc_client_server_callback_t done,
                     void* done_data, rc_client_t* client) {
        Session* s = of(client);
        if (s == nullptr || request == nullptr) return;
        std::string url = request->url ? request->url : "";
        std::string body = request->post_data ? request->post_data : "";
        std::string type = request->content_type ? request->content_type : "";
        std::lock_guard<std::mutex> guard(s->queue_lock_);
        s->outgoing_.push_back([s, url, body, type, done, done_data] {
            const HttpReply reply = http_post(url, body, type, s->agent_);
            if (reply.status == 0) SDL_Log("RetroAchievements: no reply: %s", reply.body.c_str());
            std::lock_guard<std::mutex> back(s->queue_lock_);
            s->replies_.push_back([reply, done, done_data] {
                rc_api_server_response_t response{};
                response.body = reply.body.c_str();
                response.body_length = reply.body.size();
                response.http_status_code = reply.status;
                done(&response, done_data);
            });
        });
        s->queue_wake_.notify_one();
    }

    static void log(const char* message, const rc_client_t*) {
        SDL_Log("RetroAchievements: %s", message ? message : "");
    }

    static void signed_in(int result, const char* error, rc_client_t* client, void*) {
        Session* s = of(client);
        if (s == nullptr) return;
        if (result != RC_OK) {
            s->status_ = Status::SignedOut;
            s->notify(Notice::Type::Trouble, "RetroAchievements sign-in failed",
                      error ? error : "");
            return;
        }
        s->status_ = Status::Ready;
        s->notify(Notice::Type::News, "Signed in to RetroAchievements", s->display_name());
        if (s->waiting_for_sign_in_) s->start_loading();
    }

    static void game_loaded(int result, const char* error, rc_client_t* client, void*) {
        Session* s = of(client);
        if (s == nullptr) return;
        if (result == RC_NO_GAME_LOADED) {
            s->status_ = Status::UnknownDisc;
            s->notify(Notice::Type::Trouble, "No achievements for this disc",
                      "RetroAchievements does not know this version of the game.");
            return;
        }
        if (result != RC_OK) {
            s->status_ = s->status_ == Status::SignedOut ? Status::SignedOut : Status::Ready;
            s->notify(Notice::Type::Trouble, "Could not load achievements",
                      error ? error : "");
            return;
        }
        const std::vector<Entry> list = s->entries_locked();
        if (list.empty()) {
            s->status_ = Status::NoAchievements;   // nothing wrong, nothing to show
            return;
        }
        s->status_ = Status::Active;
        const auto earned = std::count_if(list.begin(), list.end(),
                                          [](const Entry& e) { return e.earned; });
        s->notify(Notice::Type::News, s->game_name(),
                  std::to_string(earned) + " of " + std::to_string(list.size()) +
                      " achievements earned",
                  0, s->game_icon());
    }

    static void disc_swapped(int result, const char* error, rc_client_t* client, void*) {
        Session* s = of(client);
        if (s == nullptr || result == RC_OK) return;
        s->notify(Notice::Type::Trouble, "This disc is not part of the game",
                  error ? error : "");
    }

    static void event(const rc_client_event_t* e, rc_client_t* client) {
        Session* s = of(client);
        if (s == nullptr || e == nullptr) return;
        switch (e->type) {
            case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
                const rc_client_achievement_t* a = e->achievement;
                if (a == nullptr || is_pseudo_achievement(a->id)) break;
                s->notify(Notice::Type::Earned, a->title ? a->title : "",
                          a->description ? a->description : "", a->points,
                          a->badge_url ? a->badge_url : "");
                break;
            }
            case RC_CLIENT_EVENT_GAME_COMPLETED:
                s->notify(Notice::Type::News, "Set complete",
                          "Every achievement in " + s->game_name() + " earned", 0,
                          s->game_icon());
                break;
            case RC_CLIENT_EVENT_DISCONNECTED:
                s->notify(Notice::Type::Trouble, "Can't reach RetroAchievements",
                          "Achievements you earn will be sent once it's back.");
                break;
            case RC_CLIENT_EVENT_RECONNECTED:
                s->notify(Notice::Type::News, "RetroAchievements is back",
                          "Achievements earned meanwhile have been sent.");
                break;
            case RC_CLIENT_EVENT_SERVER_ERROR:
                s->notify(Notice::Type::Trouble, "RetroAchievements reported a problem",
                          e->server_error && e->server_error->error_message
                              ? e->server_error->error_message
                              : "");
                break;
            default:
                break;
        }
    }
};

// ---------------------------------------------------------------------------

Session::Session() = default;
Session::~Session() { close(); }

bool Session::open(retro3do_t* emu) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ != nullptr) return true;
    emu_ = emu;
    unavailable_ = http_unavailable();
    if (!unavailable_.empty()) return false;

    rc_client_t* client = rc_client_create(&Callbacks::read_memory, &Callbacks::send);
    if (client == nullptr) {
        unavailable_ = "rcheevos could not start";
        return false;
    }
    rc_ = client;
    rc_client_set_userdata(client, this);
    rc_client_set_event_handler(client, &Callbacks::event);
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_WARN, &Callbacks::log);
    // Casual mode. rc_client starts in hardcore, which a client with save
    // states may not use.
    rc_client_set_hardcore_enabled(client, 0);
    // Memory is read only from inside on_frame() and pump(), never by rcheevos
    // on a thread of its own.
    rc_client_set_allow_background_memory_reads(client, 0);

    agent_ = std::string("a3DO/") + A3DO_CHEEVOS_VERSION + " rcheevos/" + RCHEEVOS_VERSION_STRING;
    closing_ = false;
    network_ = std::thread([this] { run_network(); });
    status_ = Status::SignedOut;
    return true;
}

void Session::close() {
    if (network_.joinable()) {
        {
            std::lock_guard<std::mutex> guard(queue_lock_);
            closing_ = true;
        }
        queue_wake_.notify_all();
        network_.join();
    }
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ != nullptr) {
        rc_client_destroy(rc(rc_));
        rc_ = nullptr;
    }
    std::lock_guard<std::mutex> queues(queue_lock_);
    outgoing_.clear();
    replies_.clear();
    status_ = Status::Unavailable;
}

void Session::run_network() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> guard(queue_lock_);
            queue_wake_.wait(guard, [this] { return closing_ || !outgoing_.empty(); });
            if (closing_) return;
            job = std::move(outgoing_.front());
            outgoing_.pop_front();
        }
        job();
    }
}

// With lock_ held.
void Session::deliver_replies() {
    for (;;) {
        std::function<void()> reply;
        {
            std::lock_guard<std::mutex> guard(queue_lock_);
            if (replies_.empty()) return;
            reply = std::move(replies_.front());
            replies_.pop_front();
        }
        reply();
    }
}

void Session::notify(Notice::Type type, std::string title, std::string body, unsigned points,
                     std::string image) {
    std::lock_guard<std::mutex> guard(notices_lock_);
    notices_.push_back(Notice{type, std::move(title), std::move(body), points, std::move(image)});
}

std::vector<Notice> Session::take_notices() {
    std::lock_guard<std::mutex> guard(notices_lock_);
    std::vector<Notice> out;
    out.swap(notices_);
    return out;
}

Status Session::status() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return status_;
}

void Session::on_frame() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr) return;
    deliver_replies();
    rc_client_do_frame(rc(rc_));
}

void Session::pump(bool game_running) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr) return;
    deliver_replies();
    // While the game runs, on_frame() keeps the session going.
    if (!game_running) rc_client_idle(rc(rc_));
}

void Session::on_machine_reset() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ != nullptr) rc_client_reset(rc(rc_));
}

// --- account ---------------------------------------------------------------

void Session::sign_in(const std::string& user, const std::string& password) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr) return;
    status_ = Status::SigningIn;
    rc_client_begin_login_with_password(rc(rc_), user.c_str(), password.c_str(),
                                        &Callbacks::signed_in, nullptr);
}

void Session::resume_sign_in(const std::string& user, const std::string& token) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr) return;
    status_ = Status::SigningIn;
    rc_client_begin_login_with_token(rc(rc_), user.c_str(), token.c_str(),
                                     &Callbacks::signed_in, nullptr);
}

void Session::sign_out() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr) return;
    rc_client_logout(rc(rc_));   // also unloads the game
    status_ = Status::SignedOut;
    waiting_for_sign_in_ = !disc_hash_.empty();
}

std::string Session::display_name() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const rc_client_user_t* user = rc_ ? rc_client_get_user_info(rc(rc_)) : nullptr;
    if (user == nullptr) return {};
    return user->display_name ? user->display_name : (user->username ? user->username : "");
}

std::string Session::account_name() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const rc_client_user_t* user = rc_ ? rc_client_get_user_info(rc(rc_)) : nullptr;
    return user && user->username ? user->username : std::string();
}

std::string Session::token() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const rc_client_user_t* user = rc_ ? rc_client_get_user_info(rc(rc_)) : nullptr;
    return user && user->token ? user->token : std::string();
}

unsigned Session::points() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const rc_client_user_t* user = rc_ ? rc_client_get_user_info(rc(rc_)) : nullptr;
    return user ? user->score_softcore : 0;
}

// --- game ------------------------------------------------------------------

// With lock_ held.
void Session::start_loading() {
    waiting_for_sign_in_ = false;
    if (disc_hash_.empty()) return;
    status_ = Status::Loading;
    rc_client_begin_load_game(rc(rc_), disc_hash_.c_str(), &Callbacks::game_loaded, nullptr);
}

void Session::insert_disc(const std::string& hash, bool same_game) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr) return;
    const bool loaded = rc_client_get_game_info(rc(rc_)) != nullptr;
    disc_hash_ = hash;
    if (same_game && loaded) {
        rc_client_begin_change_media(rc(rc_), hash.c_str(), &Callbacks::disc_swapped, nullptr);
        return;
    }
    if (loaded) rc_client_unload_game(rc(rc_));
    if (status_ == Status::SignedOut || status_ == Status::SigningIn) {
        // Loaded by signed_in(). (rc_client could queue it behind a sign-in
        // already under way, but not behind one that has not started.)
        waiting_for_sign_in_ = true;
        return;
    }
    start_loading();
}

void Session::remove_disc() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    disc_hash_.clear();
    waiting_for_sign_in_ = false;
    if (rc_ == nullptr) return;
    rc_client_unload_game(rc(rc_));
    if (status_ != Status::SignedOut && status_ != Status::SigningIn &&
        status_ != Status::Unavailable)
        status_ = Status::Ready;
}

std::string Session::game_name() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const rc_client_game_t* game = rc_ ? rc_client_get_game_info(rc(rc_)) : nullptr;
    return game && game->title ? game->title : std::string();
}

std::string Session::game_icon() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const rc_client_game_t* game = rc_ ? rc_client_get_game_info(rc(rc_)) : nullptr;
    return game && game->badge_url ? game->badge_url : std::string();
}

std::vector<Session::Entry> Session::entries() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return entries_locked();
}

std::vector<Session::Entry> Session::entries_locked() const {
    std::vector<Entry> out;
    if (rc_ == nullptr) return out;
    rc_client_achievement_list_t* list = rc_client_create_achievement_list(
        rc(rc_), RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
    if (list == nullptr) return out;
    for (uint32_t b = 0; b < list->num_buckets; ++b) {
        const rc_client_achievement_bucket_t& bucket = list->buckets[b];
        for (uint32_t i = 0; i < bucket.num_achievements; ++i) {
            const rc_client_achievement_t* a = bucket.achievements[i];
            if (a == nullptr || is_pseudo_achievement(a->id)) continue;
            Entry e;
            e.name = a->title ? a->title : "";
            e.description = a->description ? a->description : "";
            e.progress = a->measured_progress;
            e.percent = a->measured_percent;
            e.badge = a->badge_url ? a->badge_url : "";
            e.badge_locked = a->badge_locked_url ? a->badge_locked_url : "";
            e.points = a->points;
            e.earned = (a->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE) != 0;
            e.unsupported = bucket.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED;
            out.push_back(std::move(e));
        }
    }
    rc_client_destroy_achievement_list(list);
    return out;
}

// --- save states -----------------------------------------------------------

std::vector<uint8_t> Session::save_progress() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::vector<uint8_t> blob;
    if (rc_ == nullptr || rc_client_get_game_info(rc(rc_)) == nullptr) return blob;
    blob.resize(rc_client_progress_size(rc(rc_)));
    if (blob.empty() ||
        rc_client_serialize_progress_sized(rc(rc_), blob.data(), blob.size()) != RC_OK)
        blob.clear();
    return blob;
}

bool Session::restore_progress(const std::vector<uint8_t>& blob) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (rc_ == nullptr || blob.empty() || rc_client_get_game_info(rc(rc_)) == nullptr)
        return false;
    return rc_client_deserialize_progress_sized(rc(rc_), blob.data(), blob.size()) == RC_OK;
}

// --- the kept sign-in ------------------------------------------------------

bool read_saved_login(const std::string& dir, SavedLogin& out) {
    out = SavedLogin{};
    std::ifstream file(dir + kLoginFile);
    std::getline(file, out.user);
    std::getline(file, out.token);
    for (std::string* field : {&out.user, &out.token}) {
        while (!field->empty() && std::isspace(static_cast<unsigned char>(field->back())))
            field->pop_back();
    }
    if (out.empty()) out = SavedLogin{};
    return !out.empty();
}

bool write_saved_login(const std::string& dir, const SavedLogin& login) {
    if (login.empty() || login.user.find('\n') != std::string::npos ||
        login.token.find('\n') != std::string::npos)
        return false;
    // Anyone holding the token can act as the player, so the file is
    // readable by the app alone, and complete before it replaces the old one.
    const std::string path = dir + kLoginFile;
    const std::string partial = path + ".part";
    {
        std::ofstream file(partial, std::ios::trunc);
        if (!file) return false;
#if !defined(_WIN32)
        ::chmod(partial.c_str(), 0600);
#endif
        file << login.user << '\n' << login.token << '\n';
        if (!file.flush()) return false;
    }
    if (std::rename(partial.c_str(), path.c_str()) != 0) {
        std::remove(partial.c_str());
        return false;
    }
    return true;
}

void forget_saved_login(const std::string& dir) { std::remove((dir + kLoginFile).c_str()); }

}  // namespace retro3do::cheevos
