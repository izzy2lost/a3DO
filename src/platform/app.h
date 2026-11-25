// The application shell: window, event pump, frame pacing, and the handoff of
// emulated frames to the screen.
//
// Everything platform-specific lives behind SDL, so this same file is what runs
// on Linux, Windows, Android and iOS. There is no per-platform front end to
// keep in sync, which was the point of choosing SDL over a UI toolkit that
// would have needed a bridge on each side.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "retro3do.h"
#include "app/types.h"
#include "app/cover_download.h"
#include "ui/game_library.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_AudioStream;
struct SDL_Gamepad;
union SDL_Event;

namespace retro3do {

class Ui;
class Settings;
namespace cheevos { class Session; }
class TouchPad;

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Bring up SDL, the window and the renderer. Returns false and sets
    // last_error() if anything the app cannot run without is missing.
    // Files named on the command line, which override anything remembered from
    // a previous run. Desktop-only in practice - Android and iOS have no
    // argv worth reading - but it is what makes the emulator scriptable, and
    // scriptable is what makes it testable outside a GUI.
    void set_launch_files(std::string bios, std::string disc);
    void set_launch_demo(bool enabled) { launch_demo_ = enabled; }

    // Run without a person in front of it for this many seconds, then report
    // what the machine did and quit.
    //
    // A front end is the one part of an emulator that cannot be unit tested,
    // and "it compiled" says nothing about whether frames are reaching the
    // screen. This runs the whole application - window, event pump, emulator
    // thread, presentation - under SDL_VIDEODRIVER=offscreen and prints what
    // arrived, so a build machine with no display can still tell a working
    // application from one that draws nothing.
    void set_selftest_seconds(double seconds) { selftest_seconds_ = seconds; }

    bool init();

    // Run until the user quits. Returns the process exit code.
    int run();

    // What a self-test run saw. Zero emulated frames, or a picture that never
    // stopped being one flat colour, is a failure whatever else happened.
    struct SelfTestResult {
        u64 frames_emulated = 0;
        u64 display_frames = 0;
        double emulated_fps = 0.0;
        int lit_pixels = 0;   // pixels differing from the frame's commonest colour
        bool ok = false;
    };
    const SelfTestResult& selftest_result() const { return selftest_; }

    const std::string& last_error() const { return last_error_; }

private:
    static bool lifecycle_event_watch(void* userdata, SDL_Event* event);

    // One turn of the loop: events, emulate, draw. Split out so that platforms
    // which insist on driving the loop themselves can call it directly.
    void tick();

    void handle_events();
    bool start_audio();
    void stop_audio();
    void feed_audio();
    void apply_keyboard(int scancode, bool down);
    void open_gamepad(u32 which);
    void close_gamepad(u32 which);
    // Which chained pad a host controller drives, or -1 if it is not one of
    // ours. Controllers keep the slot they were given for as long as they stay
    // plugged in, so unplugging player two does not shuffle player three.
    int pad_slot_for(u32 joystick_id) const;
    bool is_remote(u32 joystick_id) const;
    void apply_gamepad_button(int slot, int button, bool down);
    void apply_gamepad_axis(int slot, int axis, s16 value);
    void load_settings();
    void save_settings();
    void load_library();
    void save_library();
    void load_retro_media_artwork();
    void remember_game(const std::string& name, const std::string& target);
    bool load_builtin_bios();
    void apply_bios_selection();
    void use_custom_bios(const std::string& path, const std::string& name);
    void begin_bios_change();
    void finish_bios_change();
    void scan_games_folder(const std::string& name, const std::string& target);
    // Box art lives in <private storage>/covers. A rescan copies in any images
    // from a "covers" folder inside the games folder; refresh hands the list
    // to the carousel.
    void import_covers(const std::string& games_folder);
    void refresh_covers();
    void start_cover_download();
    void load_nvram();
    void save_nvram();
    // Make the given game's own NVRAM the live one, writing out the one it
    // replaces. A game with none yet starts from a copy of the shared image.
    void use_game_nvram(const std::string& disc_title);
    bool open_bios(const std::string& path, const std::string& name);
    bool open_disc(const std::string& path, const std::string& name);
    void save_state_slot(int slot);
    void load_state_slot(int slot);
    void present();
    // RetroAchievements: bring the session up and sign in with the kept
    // token; identify the disc in the drive (a swap keeps the game loaded);
    // and each frame hand the UI its status, list and pop-ups.
    void start_cheevos();
    void identify_disc_for_cheevos(bool swap);
    void update_cheevos();
    void update_gesture_exclusion();
    void refresh_screen_shape();
    void ensure_frame_texture(int width, int height);
    void suspend_renderer();
    bool resume_renderer();

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  frame_texture_ = nullptr;
    int texture_width_  = 0;
    int texture_height_ = 0;

    // The machine, the thread it runs on and the mailbox its frames arrive in
    // are all behind this one handle. The application does not own an emulator
    // thread any more: it asks the core to start and stop one, which is what
    // keeps the pacing policy in a single place rather than in every front end
    // that wraps this core.
    retro3do_t* emu_ = nullptr;

    std::unique_ptr<Ui> ui_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<TouchPad> touch_;
    std::unique_ptr<CoverDownloader> cover_download_;
    std::unique_ptr<cheevos::Session> cheevos_;
    bool cheevos_token_saved_ = false;   // this sign-in's token is on disk
    int cheevos_refresh_ = 0;            // frames until the summary is re-read
    int covers_shown_ = 0;   // fetched covers already passed to the UI

    SDL_AudioStream* audio_stream_ = nullptr;
    bool audio_started_ = false;
    // One host controller per chained 3DO pad. Slot zero is also what the
    // keyboard and the on-screen controls drive, so it is never freed.
    SDL_Gamepad* gamepads_[RETRO3DO_MAX_PADS] = {};
    // TV remotes and similar d-pad-only devices that Android reports as
    // gamepads (an IR receiver, a Bluetooth remote, a search key). They take
    // no pad of their own, which would push a real controller to pad 2 or
    // later; their presses go to pad 1, as the keyboard's do.
    std::vector<SDL_Gamepad*> remotes_;
    // Start+Select held together opens the menu. Per pad: which of the two
    // are held (1 Start, 2 Select), and whether this hold already fired, so
    // the buttons do not also reach the game until both are let go.
    u8 menu_combo_held_[RETRO3DO_MAX_PADS] = {};
    bool menu_combo_fired_[RETRO3DO_MAX_PADS] = {};

    // Which way each analogue axis is currently being held, per pad. Kept so a
    // stick resting near the threshold cannot chatter: crossing out of a
    // direction needs a smaller push than crossing into it.
    s8 axis_direction_[RETRO3DO_MAX_PADS][2] = {};

    std::string launch_bios_;
    std::string launch_disc_;
    std::string actual_renderer_;
    std::string renderer_startup_message_;
    double selftest_seconds_ = 0.0;
    u64 selftest_started_ms_ = 0;
    u64 display_frames_ = 0;
    bool start_on_launch_ = false;
    bool launch_demo_ = false;
    bool running_ = false;
    bool emulating_ = false;
    std::atomic<bool> backgrounded_{false};
    std::atomic<u64> render_resume_after_ms_{0};
    bool lifecycle_watch_registered_ = false;
    bool lifecycle_pause_applied_ = false;
    bool renderer_suspended_ = false;
    GameLibrary library_;
    int persisted_library_count_ = 0;
    // Reused across saves so a save does not allocate thirty-two kilobytes.
    std::vector<u8> nvram_scratch_;
    // The file the live NVRAM is saved to: the shared nvram.bin until a game
    // starts, then that game's own (game_nvram_path).
    std::string nvram_path_;
    std::string disc_title_;       // the disc in the drive, as its slots are named
    std::string save_state_dir_;   // the disc in the drive's slots; empty with none
    // The touch controls' boxes as last handed to Android, in pixels, so the
    // system is only told again when they move.
    std::vector<int> gesture_exclusion_;
    // Set when the window or its insets change; the cutout's exact shape is
    // then fetched again for the menus (Ui::set_screen_shape).
    bool screen_shape_stale_ = true;

    SelfTestResult selftest_;

    std::string last_error_;
};

}  // namespace retro3do
