// a3DO front end: splash, library, settings and in-game quick menu.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "retro3do.h"
#include "app/types.h"
#include "ui/file_browser.h"
#include "ui/cover_art.h"
#include "ui/game_library.h"
#include <imgui.h>

struct ImGuiStyle;
struct ImVec2;
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;

namespace retro3do {

struct RetroMediaArtwork;
struct RetroMediaGame;
class TouchPad;

// What the UI asks the application to do. It is consumed once per display
// frame, keeping storage and machine mutations out of the drawing code.
struct UiIntent {
    bool quit = false;
    bool download_covers = false;
    bool cancel_cover_download = false;
    bool reset = false;
    bool start_demo = false;
    bool toggle_pause = false;
    bool bios_chosen = false;      // desktop: a BIOS path from the file browser
    bool pick_bios = false;        // Android: open the system file picker
    bool use_builtin_bios = false; // return to the bundled opera-boot ROM
    std::string bios_path;
    std::string bios_name;
    bool disc_chosen = false;
    bool start_disc = false;
    std::string disc_path;
    std::string disc_name;
    bool eject = false;
    bool toggle_touch_controls = false;
    bool toggle_layout_edit = false;
    bool reset_touch_layout = false;
    bool touch_layout_changed = false;
    bool region_changed = false;
    bool set_region_pal = false;
    bool cpu_scale_changed = false;
    u32 cpu_scale_percent = 100;
    bool render_scale_changed = false;
    u32 render_scale = 1;
    bool ui_settings_changed = false;
    bool games_folder_chosen = false;
    bool setup_finished = false;
    bool rescan_games = false;
    bool import_gpu_driver = false;
    bool use_system_gpu_driver = false;
    bool retro_media_login = false;
    bool retro_media_logout = false;
    bool retro_media_sync = false;
    bool retro_media_artwork_changed = false;
    bool retro_media_catalogue = false;
    bool retro_media_download = false;
    std::string retro_media_email;
    std::string retro_media_password;
    std::string retro_media_search;
    std::string retro_media_slug;
    std::string folder_path;
    std::string folder_name;
    int save_state_slot = 0;   // 1..10: save the running game there
    int load_state_slot = 0;   // 1..10: put the running game back from there
};

class Ui {
public:
    Ui();
    ~Ui();

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void hide_launcher();
    void show_launcher() { show_launcher_ = true; }

    void set_remembered_disc(const std::string& name, const std::string& target);
    void set_game_library(const std::vector<LibraryGame>& games);
    // Box art for the carousel: image files (bare names) inside `directory`.
    void set_cover_files(const std::string& directory,
                         const std::vector<std::string>& files);
    void set_cover_download_status(bool running, int done, int total,
                                   const std::string& message);
    bool cover_view() const { return cover_view_; }
    void set_cover_view(bool on) { cover_view_ = on; }
    void configure_setup(bool complete, const std::string& games_folder_name,
                         const std::string& games_folder_target);
    // Which BIOS the machine is running: the user's own (custom) or the
    // built-in opera-boot. `message` explains a fallback or failure.
    void set_bios_status(bool custom, const std::string& name,
                         const std::string& message);
    // The running game was stopped by the application (e.g. a BIOS change).
    void end_session();
    // Save states: the folder holding the running game's ten slots (empty
    // with no game), and what became of a save or load the menu asked for.
    void set_save_state_dir(const std::string& dir);
    // The screen's real shape, in window points: the system bars' insets
    // (x left, y top, z right, w bottom) and each display cutout's exact box
    // (x, y, w, h). Without it the menus keep to SDL's safe area, which
    // treats a camera hole as a strip the full length of its edge.
    void set_screen_shape(const ImVec4& bars, const std::vector<ImVec4>& holes);
    void save_state_finished(int slot, bool ok, const std::string& message);
    void load_state_finished(int slot, bool ok, const std::string& message);
    void set_performance_visible(bool visible) { performance_visible_ = visible; }
    bool performance_visible() const { return performance_visible_; }
    void configure_video(int renderer_backend, bool widescreen, bool bezel,
                         bool crt_effect, const std::string& gpu_driver_name);
    void set_renderer_status(const std::string& actual,
                             const std::string& startup_message);
    void set_gpu_driver_status(const std::string& name,
                               const std::string& message);
    void configure_retro_media(const std::string& media_type);
    void set_retro_media_status(const std::string& email, int credits,
                                int free_remaining, bool is_admin, bool busy,
                                const std::string& message);
    void set_retro_media_error(const std::string& message);
    void set_retro_media_email_hint(const std::string& email);
    void set_retro_media_artwork(
        const std::vector<RetroMediaArtwork>& artwork);
    void set_retro_media_catalogue(const std::vector<RetroMediaGame>& games);
    int renderer_backend() const { return renderer_backend_; }
    bool widescreen() const { return widescreen_; }
    bool bezel() const { return bezel_; }
    // Where the touch controls go: the whole screen in landscape, the space
    // under the picture in portrait.
    void touch_area(ImVec2* pos, ImVec2* size) const;
    void launcher_area(ImVec2* pos, ImVec2* size) const;
    bool crt_effect() const { return crt_effect_; }
    const std::string& gpu_driver_name() const { return gpu_driver_name_; }
    const std::string& retro_media_type() const { return retro_media_type_; }
    void suspend_renderer();
    bool resume_renderer(SDL_Renderer* renderer);
    void shutdown();

    void process_event(const SDL_Event& event);
    bool wants_mouse() const;
    bool wants_keyboard() const;

    UiIntent build(retro3do_t* emu, bool emulating, double display_fps,
                   double emulated_fps, double frame_ms, u64 underruns,
                   TouchPad& touch);
    void render(SDL_Renderer* renderer);

private:
    enum class Page { Library, Settings, Downloads, System, About };
    enum class Browsing { None, Bios, Disc };

    void release_ui_texture();
    void update_scale();
    void apply_safe_area();
    void update_touch_scroll();
    void draw_splash();
    void draw_setup_wizard(UiIntent& intent);
    void poll_setup_picker(UiIntent& intent);
    void draw_launcher(retro3do_t* emu, bool emulating, bool touch_visible,
                       bool touch_editing, UiIntent& intent);
    void draw_library(retro3do_t* emu, UiIntent& intent);
    void draw_cover_download(UiIntent& intent, float width);
    struct CoverTexture;
    // A grid card's box art, fitted into the area at `pos`, `size`. A tap
    // turns it over to show the back; another tap turns it back.
    void draw_grid_cover(ImDrawList* draw, const CoverTexture& cover,
                         const std::string& key, ImVec2 pos, ImVec2 size);
    void draw_cover_flow(retro3do_t* emu,
                         const std::vector<const LibraryGame*>& games,
                         UiIntent& intent);
    void launch_game(const LibraryGame& game, UiIntent& intent);
    void draw_settings(retro3do_t* emu, bool touch_visible, bool touch_editing,
                       UiIntent& intent);
    void draw_bios_panel(UiIntent& intent);
    void draw_render_scale(retro3do_t* emu, UiIntent& intent, bool compact);
    void draw_pad_editor(TouchPad& touch, UiIntent& intent);
    void draw_downloads(UiIntent& intent);
    void draw_system(retro3do_t* emu, UiIntent& intent);
    void draw_about();
    void draw_overlay(retro3do_t* emu, bool emulating, double display_fps,
                      double emulated_fps, double frame_ms, u64 underruns,
                      bool touch_visible, bool touch_editing,
                      const TouchPad& touch, UiIntent& intent);
    void draw_quick_menu(retro3do_t* emu, bool emulating, bool touch_visible,
                         bool touch_editing, UiIntent& intent);
    void open_quick_menu(bool emulating, UiIntent& intent);
    void draw_save_states(UiIntent& intent);
    void refresh_save_slots();
    void release_save_slot_textures();
    void wake_menu_button();
    void draw_file_browser(UiIntent& intent);
    SDL_Texture* artwork_texture(const std::string& game_name);
    void release_artwork_textures();
    void clear_artwork_textures();

    // One decoded cover on the GPU. Textures far from the carousel's
    // position are released and decoded again if the user comes back.
    struct CoverTexture {
        SDL_Texture* front = nullptr;
        SDL_Texture* back = nullptr;
        SDL_Texture* spine = nullptr;
        float front_aspect = 0.705f;
        float back_aspect = 0.705f;
        float spine_aspect = 0.0f;    // width / height; 0 without a spine
        float front_banner = 0.0f;    // see CoverImage::front_banner
        bool requested = false;
        bool failed = false;
        u64 last_used = 0;
    };
    struct SaveSlot {
        bool used = false;
        std::string when;          // "Nov 21, 1:27 PM"
        SDL_Texture* thumb = nullptr;
        float thumb_aspect = 4.0f / 3.0f;
    };
    std::string save_state_dir_;
    SaveSlot save_slots_[10];
    bool save_states_page_ = false;
    bool have_screen_shape_ = false;
    ImVec4 screen_bars_{0, 0, 0, 0};
    std::vector<ImVec4> screen_holes_;
    bool compact_ = false;           // short landscape screen: slimmer header
    bool save_slots_stale_ = true;
    int save_state_busy_ = 0;        // slot waiting on the app, 0 for none
    int save_confirm_slot_ = 0;      // slot whose SAVE was tapped once
    u64 save_confirm_ms_ = 0;
    int save_preview_slot_ = 0;      // slot shown full size
    std::string save_state_message_;
    bool save_state_message_ok_ = true;
    u64 save_state_message_ms_ = 0;
    bool close_menu_requested_ = false;

    const CoverTexture* cover_for(const std::string& game_name);
    void pump_covers();
    void release_cover_textures();

    struct ArtworkTexture {
        std::string path;
        int width = 0;
        int height = 0;
        SDL_Texture* texture = nullptr;
        bool attempted = false;
    };

    FileBrowser browser_;
    GameLibrary library_;
    Browsing browsing_ = Browsing::None;
    Page page_ = Page::Library;
    Page drawn_page_ = Page::Library;

    SDL_Window* window_ = nullptr;
    std::unique_ptr<ImGuiStyle> base_style_;
    float scale_ = 1.0f;
    bool initialised_ = false;
    bool show_launcher_ = true;
    bool setup_complete_ = true;
    bool session_available_ = false;
    bool splash_complete_ = false;
    bool quick_menu_ = false;
    bool resume_after_menu_ = false;
    bool performance_visible_ = false;
    int renderer_backend_ = 0;
    bool widescreen_ = false;
    bool bezel_ = false;
    bool crt_effect_ = false;
    bool menu_requested_ = false;
    u64 splash_started_ms_ = 0;
    u64 last_pointer_activity_ms_ = 0;
    u32 scroll_window_ = 0;
    float scroll_start_y_ = 0.0f;
    float scroll_start_offset_ = 0.0f;
    bool scroll_dragging_ = false;
    char search_buffer_[128] = {};
    char library_group_ = 0;
    bool picking_games_folder_ = false;
    bool bios_custom_ = false;
    std::string bios_name_;
    std::string bios_message_;
    std::string games_folder_name_;
    std::string games_folder_target_;
    std::string actual_renderer_;
    std::string renderer_startup_message_;
    std::string gpu_driver_name_;
    std::string gpu_driver_message_;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* ui_texture_ = nullptr;
    int ui_texture_width_ = 0;
    int ui_texture_height_ = 0;
    std::unordered_map<std::string, ArtworkTexture> artwork_;
    CoverIndex cover_index_;
    std::unique_ptr<CoverDecoder> cover_decoder_;
    std::unordered_map<std::string, std::string> cover_paths_;   // title -> file
    std::unordered_map<std::string, CoverTexture> cover_textures_;  // file -> GPU
    u64 cover_frame_ = 0;
    bool cover_view_ = true;
    bool cover_download_running_ = false;
    int cover_download_done_ = 0, cover_download_total_ = 0;
    std::string cover_download_message_;
    float carousel_pos_ = 0.0f;       // fractional index under the centre
    int carousel_target_ = 0;         // index the carousel is settling on
    float carousel_velocity_ = 0.0f;  // covers per second, while dragging
    float carousel_drag_x_ = 0.0f;
    float carousel_drag_pos_ = 0.0f;
    bool carousel_moved_ = false;
    int flip_index_ = -1;             // the cover that is turned or turning
    bool flipped_ = false;            // want the back of flip_index_ showing
    float flip_angle_ = 0.0f;         // radians, 0 = front, pi = back
    // Grid cards turned over by a tap, by game target. A card is dropped from
    // here once it has turned back to its front.
    struct GridFlip {
        bool back = false;
        float angle = 0.0f;           // radians, 0 = front, pi = back
    };
    std::unordered_map<std::string, GridFlip> grid_flips_;
    std::string retro_media_type_ = "box2d";
    std::string retro_media_email_;
    std::string retro_media_message_ = "Not signed in";
    int retro_media_credits_ = 0;
    int retro_media_free_remaining_ = 0;
    bool retro_media_busy_ = false;
    bool retro_media_admin_ = false;
    char retro_media_email_buffer_[192] = {};
    char retro_media_password_buffer_[192] = {};
    char retro_media_search_buffer_[128] = {};
    char retro_media_group_ = 0;
    struct CatalogueItem {
        std::string slug;
        std::string name;
        int rom_files = 0;
        long long total_bytes = 0;
    };
    std::vector<CatalogueItem> retro_media_catalogue_;

    std::vector<LibraryDisc> active_discs_;
    size_t active_disc_ = 0;

    char disc_path_buffer_[512] = {};
    std::string disc_target_;
};

}  // namespace retro3do
