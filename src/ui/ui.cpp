#include "ui.h"
#include "ui/touch_pad.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "retro3do.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "platform/android_storage.h"
#include "platform/platform.h"
#include "ui/embedded_font.h"

namespace retro3do {
namespace {

// The theme takes its colours from the console's era: deep charcoal, with the
// red, gold and blue of 3DO-branded hardware and packaging as the accents.
const ImVec4 kRed(0.89f, 0.15f, 0.17f, 1.0f);      // #E3262B
const ImVec4 kGold(0.99f, 0.74f, 0.10f, 1.0f);     // #FCBD1A
const ImVec4 kBlue(0.27f, 0.47f, 0.98f, 1.0f);     // #4578FA
const ImVec4 kGreen(0.30f, 0.82f, 0.50f, 1.0f);
const ImVec4 kAmber(1.00f, 0.56f, 0.20f, 1.0f);    // warnings
const ImVec4 kError(1.00f, 0.38f, 0.36f, 1.0f);
const ImVec4 kText(0.95f, 0.95f, 0.97f, 1.0f);
const ImVec4 kMuted(0.60f, 0.60f, 0.67f, 1.0f);

ImU32 colour(const ImVec4& value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

// A red, gold and blue rule: the theme's signature, used under headings, the
// navigation bar and the splash.
void draw_tricolour_bar(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {
    const float third = (max.x - min.x) / 3.0f;
    draw->AddRectFilled(min, ImVec2(min.x + third, max.y), colour(kRed));
    draw->AddRectFilled(ImVec2(min.x + third, min.y),
                        ImVec2(min.x + third * 2.0f, max.y), colour(kGold));
    draw->AddRectFilled(ImVec2(min.x + third * 2.0f, min.y), max, colour(kBlue));
}

// Lays the tricolour bar into the current window's layout at `height` points.
void tricolour_rule(float width, float height) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    draw_tricolour_bar(ImGui::GetWindowDrawList(), at,
                       ImVec2(at.x + width, at.y + height));
    ImGui::Dummy(ImVec2(width, height));
}

// The a3DO logo. Sizes are its height; the text wordmark stands in if the
// image could not be made into a texture.
SDL_Texture* g_logo = nullptr;
float g_logo_aspect = 1.0f;

void load_logo(SDL_Renderer* renderer) {
    if (g_logo != nullptr || renderer == nullptr) return;
    SDL_IOStream* io = SDL_IOFromConstMem(kA3doLogoPng, kA3doLogoPng_size);
    SDL_Surface* surface = io != nullptr ? SDL_LoadPNG_IO(io, true) : nullptr;
    if (surface == nullptr) return;
    g_logo = SDL_CreateTextureFromSurface(renderer, surface);
    if (g_logo != nullptr) {
        SDL_SetTextureScaleMode(g_logo, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(g_logo, SDL_BLENDMODE_BLEND);
        g_logo_aspect = static_cast<float>(surface->w) /
                        static_cast<float>(std::max(1, surface->h));
    }
    SDL_DestroySurface(surface);
}

void release_logo() {
    if (g_logo != nullptr) SDL_DestroyTexture(g_logo);
    g_logo = nullptr;
}

float wordmark_width(float height) {
    if (g_logo != nullptr) return height * g_logo_aspect;
    return ImGui::GetFont()->CalcTextSizeA(height, FLT_MAX, 0.0f, "a3DO").x;
}

void draw_wordmark(ImDrawList* draw, ImVec2 at, float height,
                   float alpha = 1.0f) {
    if (g_logo != nullptr) {
        draw->AddImage(
            ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(g_logo))),
            at, ImVec2(at.x + height * g_logo_aspect, at.y + height),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
        return;
    }
    ImFont* font = ImGui::GetFont();
    const char* letters[] = {"a", "3", "D", "O"};
    const ImVec4 colours[] = {kText, kRed, kGold, kBlue};
    for (int i = 0; i < 4; ++i) {
        ImVec4 shade = colours[i];
        shade.w *= alpha;
        draw->AddText(font, height, at, colour(shade), letters[i]);
        at.x += font->CalcTextSizeA(height, FLT_MAX, 0.0f, letters[i]).x;
    }
}

void page_heading(const char* title, const char* subtitle) {
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextColored(kText, "%s", title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted, "%s", subtitle);
    ImGui::PopTextWrapPos();
    tricolour_rule(std::min(150.0f * ImGui::GetIO().FontGlobalScale,
                            ImGui::GetContentRegionAvail().x),
                   3.0f * ImGui::GetIO().FontGlobalScale);
    ImGui::Spacing();
}

ImVec4 tinted(const ImVec4& colour_value, float strength, float alpha = 1.0f);

void same_line_if_fits(const char* label) {
    const float width = ImGui::CalcTextSize(label, nullptr, true).x +
        std::max(ImGui::GetFrameHeight(), ImGui::GetStyle().FramePadding.x * 2.0f) +
        ImGui::GetStyle().ItemInnerSpacing.x;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
        ImGui::SameLine();
}

void begin_panel(const char* id, float width, bool stacked) {
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders |
        (stacked ? ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize : 0);
    ImGui::BeginChild(id, ImVec2(width, 0.0f), flags, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushTextWrapPos(0.0f);
}

void end_panel() {
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void alphabet_filter(char& selected, const ImVec4& accent, float scale) {
    constexpr char groups[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(3.0f * scale, 5.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f * scale, 8.0f * scale));
    const float available = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const int max_columns = std::max(1, static_cast<int>((available + gap) /
                                                       (26.0f * scale + gap)));
    const int rows = (27 + max_columns - 1) / max_columns;
    const int columns = (27 + rows - 1) / rows;
    const float width = (available - gap * (columns - 1)) / columns;
    for (int i = 0; i < 27; ++i) {
        if (i % columns != 0) ImGui::SameLine();
        const char group = groups[i];
        char label[2] = {group, '\0'};
        if (selected == group)
            ImGui::PushStyleColor(ImGuiCol_Button, tinted(accent, 0.38f));
        const bool pressed = ImGui::Button(group == '#' ? "0-9" : label,
                                           ImVec2(width, 0.0f));
        if (selected == group) ImGui::PopStyleColor();
        if (pressed) selected = selected == group ? 0 : group;
    }
    ImGui::PopStyleVar(2);
    ImGui::Spacing();
}

bool wide_button(const char* label, float height) {
    return ImGui::Button(label, ImVec2(-1.0f, height));
}

char card_initial(const std::string& name) {
    for (unsigned char c : name) {
        if (std::isalnum(c)) return static_cast<char>(std::toupper(c));
    }
    return '?';
}

ImVec4 card_accent(const std::string& name) {
    static const ImVec4 palette[] = {kRed, kGold, kBlue};
    unsigned hash = 2166136261u;
    for (unsigned char c : name) hash = (hash ^ c) * 16777619u;
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

ImVec4 tinted(const ImVec4& colour_value, float strength, float alpha) {
    return ImVec4(0.055f + colour_value.x * strength,
                  0.055f + colour_value.y * strength,
                  0.068f + colour_value.z * strength, alpha);
}

void draw_card_title(ImDrawList* draw, const std::string& title,
                     const ImVec2& position, float max_width, ImU32 colour_value,
                     float font_size) {
    // Keep titles to one draw command. Thousands of tiny per-glyph geometry
    // submissions made the SDL renderer drop later commands on dense library
    // screens, which showed up as alternating cards with no title.
    ImFont* font = ImGui::GetFont();
    std::string shown = title;
    const char* suffix = "...";
    const float width_limit = std::max(1.0f, max_width);
    if (font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, shown.c_str()).x >
        width_limit) {
        while (!shown.empty()) {
            shown.pop_back();
            const std::string candidate = shown + suffix;
            if (font->CalcTextSizeA(font_size, FLT_MAX, 0.0f,
                                    candidate.c_str()).x <= width_limit) {
                shown = candidate;
                break;
            }
        }
    }
    const float shown_width =
        font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, shown.c_str()).x;
    draw->AddText(font, font_size,
                  ImVec2(position.x + std::max(0.0f,
                                               (width_limit - shown_width) * 0.5f),
                         position.y),
                  colour_value, shown.c_str());
}

}  // namespace

Ui::Ui() = default;

Ui::~Ui() {
    shutdown();
}

bool Ui::init(SDL_Window* window, SDL_Renderer* renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
#if RETRO3DO_MOBILE
    // iPad and iPhone want this for the same reason Android does: it widens
    // ImGui's hit areas to finger size. Not on macOS, which has a pointer.
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
#endif
    io.IniFilename = nullptr;

    // Roboto Medium, compiled in (third_party/imgui/misc/fonts, Apache-2.0).
    // ImGui rasterises it at whatever size is drawn, so text stays sharp at
    // phone and tablet scales where the default bitmap font turns blocky.
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(kRobotoMediumTtf),
        static_cast<int>(kRobotoMediumTtf_size), 15.0f, &font_config);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildRounding = 12.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameRounding = 8.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.WindowPadding = ImVec2(18.0f, 18.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.043f, 0.043f, 0.055f, 1.0f);       // #0B0B0E
    c[ImGuiCol_ChildBg] = ImVec4(0.078f, 0.078f, 0.098f, 1.0f);        // #141419
    c[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.090f, 0.114f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.17f, 0.17f, 0.21f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.17f, 0.17f, 0.21f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.19f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.23f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.13f, 0.13f, 0.165f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.10f, 0.11f, 1.0f);
    c[ImGuiCol_ButtonActive] = kRed;
    c[ImGuiCol_Header] = ImVec4(0.14f, 0.22f, 0.45f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.29f, 0.58f, 1.0f);
    c[ImGuiCol_HeaderActive] = kBlue;
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_SliderGrab] = kGold;
    c[ImGuiCol_SliderGrabActive] = kRed;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.27f, 0.47f, 0.98f, 0.35f);
    c[ImGuiCol_NavCursor] = kGold;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;

    // Bridge logical points to framebuffer pixels on Retina displays.
    const float density = SDL_GetWindowPixelDensity(window);
    if (density > 0.0f) SDL_SetRenderScale(renderer, density, density);
    window_ = window;
    base_style_ = std::make_unique<ImGuiStyle>(style);
    update_scale();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) return false;
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) return false;

    renderer_ = renderer;
    load_logo(renderer_);

    splash_started_ms_ = SDL_GetTicks();
    last_pointer_activity_ms_ = splash_started_ms_;
    initialised_ = true;
    return true;
}

void Ui::wake_menu_button() {
    last_pointer_activity_ms_ = SDL_GetTicks();
}

void Ui::hide_launcher() {
    show_launcher_ = false;
    session_available_ = true;
    wake_menu_button();
}

void Ui::set_remembered_disc(const std::string& name,
                             const std::string& target) {
    std::snprintf(disc_path_buffer_, sizeof(disc_path_buffer_), "%s",
                  name.empty() ? target.c_str() : name.c_str());
    disc_target_ = target;
}

void Ui::set_game_library(const std::vector<LibraryGame>& games) {
    library_.replace(games);
}

void Ui::configure_video(int renderer_backend, bool widescreen, bool bezel,
                         bool crt_effect, const std::string& gpu_driver_name) {
    renderer_backend_ = renderer_backend == 1 ? 1 : 0;
    widescreen_ = widescreen;
    bezel_ = bezel;
    crt_effect_ = crt_effect;
    gpu_driver_name_ = gpu_driver_name;
}

void Ui::set_renderer_status(const std::string& actual,
                             const std::string& startup_message) {
    actual_renderer_ = actual;
    renderer_startup_message_ = startup_message;
}

void Ui::set_gpu_driver_status(const std::string& name,
                               const std::string& message) {
    gpu_driver_name_ = name;
    gpu_driver_message_ = message;
}

void Ui::configure_retro_media(const std::string& media_type) {
    static constexpr const char* kTypes[] = {
        "box2d", "images", "thumbnails", "titles"
    };
    retro_media_type_ = "box2d";
    for (const char* type : kTypes) {
        if (media_type == type) retro_media_type_ = type;
    }
}

void Ui::set_retro_media_status(const std::string& email, int credits,
                                int free_remaining, bool is_admin, bool busy,
                                const std::string& message) {
    retro_media_email_ = email;
    retro_media_credits_ = credits;
    retro_media_free_remaining_ = free_remaining;
    retro_media_admin_ = is_admin;
    if (!is_admin && page_ == Page::Downloads) page_ = Page::Library;
    retro_media_busy_ = busy;
    retro_media_message_ = message;
    if (!email.empty()) {
        std::snprintf(retro_media_email_buffer_,
                      sizeof(retro_media_email_buffer_), "%s", email.c_str());
    }
    if (!busy) {
        // The password is only needed to make the server session. Do not keep
        // it in the UI buffer after that attempt succeeds or fails.
        std::memset(retro_media_password_buffer_, 0,
                    sizeof(retro_media_password_buffer_));
    }
}

void Ui::set_retro_media_catalogue(
    const std::vector<RetroMediaGame>& games) {
    retro_media_catalogue_.clear();
    retro_media_catalogue_.reserve(games.size());
    for (const RetroMediaGame& game : games) {
        retro_media_catalogue_.push_back(
            {game.slug, game.name, game.rom_files, game.total_bytes});
    }
}

void Ui::set_retro_media_error(const std::string& message) {
    retro_media_busy_ = false;
    retro_media_message_ = message;
    std::memset(retro_media_password_buffer_, 0,
                sizeof(retro_media_password_buffer_));
}

void Ui::set_retro_media_email_hint(const std::string& email) {
    if (email.empty() || retro_media_email_buffer_[0] != '\0') return;
    std::snprintf(retro_media_email_buffer_, sizeof(retro_media_email_buffer_),
                  "%s", email.c_str());
}

void Ui::clear_artwork_textures() {
    release_artwork_textures();
    artwork_.clear();
}

void Ui::release_artwork_textures() {
    for (auto& [name, item] : artwork_) {
        (void)name;
        if (item.texture != nullptr) SDL_DestroyTexture(item.texture);
        item.texture = nullptr;
        item.attempted = false;
    }
}

void Ui::set_retro_media_artwork(
    const std::vector<RetroMediaArtwork>& artwork) {
    clear_artwork_textures();
    for (const RetroMediaArtwork& item : artwork) {
        if (item.key.empty() || item.path.empty() || item.width <= 0 ||
            item.height <= 0) {
            continue;
        }
        artwork_.emplace(item.key, ArtworkTexture{item.path, item.width,
                                                  item.height, nullptr, false});
    }
}

SDL_Texture* Ui::artwork_texture(const std::string& game_name) {
    const auto found = artwork_.find(game_name);
    if (found == artwork_.end()) return nullptr;
    ArtworkTexture& item = found->second;
    if (item.texture != nullptr || item.attempted || renderer_ == nullptr) {
        return item.texture;
    }
    item.attempted = true;

    size_t size = 0;
    void* file = SDL_LoadFile(item.path.c_str(), &size);
    if (file == nullptr || size < 12) {
        if (file != nullptr) SDL_free(file);
        return nullptr;
    }
    const u8* bytes = static_cast<const u8*>(file);
    const auto big_endian_u32 = [](const u8* value) {
        return (static_cast<u32>(value[0]) << 24u) |
               (static_cast<u32>(value[1]) << 16u) |
               (static_cast<u32>(value[2]) << 8u) |
               static_cast<u32>(value[3]);
    };
    const u32 width = big_endian_u32(bytes + 4);
    const u32 height = big_endian_u32(bytes + 8);
    const size_t pixel_bytes = static_cast<size_t>(width) * height * 4u;
    if (std::memcmp(bytes, "R3AR", 4) != 0 || width == 0 || height == 0 ||
        width > 2048 || height > 2048 || pixel_bytes != size - 12u) {
        SDL_free(file);
        return nullptr;
    }

    item.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC,
                                     static_cast<int>(width),
                                     static_cast<int>(height));
    if (item.texture != nullptr) {
        SDL_UpdateTexture(item.texture, nullptr, bytes + 12,
                          static_cast<int>(width * 4u));
        SDL_SetTextureScaleMode(item.texture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(item.texture, SDL_BLENDMODE_BLEND);
        item.width = static_cast<int>(width);
        item.height = static_cast<int>(height);
    }
    SDL_free(file);
    return item.texture;
}

void Ui::configure_setup(bool complete, const std::string& games_folder_name,
                         const std::string& games_folder_target) {
    setup_complete_ = complete;
    games_folder_name_ = games_folder_name;
    games_folder_target_ = games_folder_target;
}

void Ui::set_bios_status(bool custom, const std::string& name,
                         const std::string& message) {
    bios_custom_ = custom;
    bios_name_ = name;
    bios_message_ = message;
}

void Ui::end_session() {
    session_available_ = false;
    quick_menu_ = false;
    active_discs_.clear();
    active_disc_ = 0;
}

void Ui::shutdown() {
    if (!initialised_) return;
    clear_artwork_textures();
    release_ui_texture();
    release_logo();
    if (renderer_ != nullptr) ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    renderer_ = nullptr;
    initialised_ = false;
}

void Ui::suspend_renderer() {
    if (!initialised_ || renderer_ == nullptr) return;
    release_artwork_textures();
    release_ui_texture();
    release_logo();
    ImGui_ImplSDLRenderer3_Shutdown();
    renderer_ = nullptr;
}

bool Ui::resume_renderer(SDL_Renderer* renderer) {
    if (!initialised_ || renderer == nullptr || renderer_ != nullptr) {
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) return false;
    renderer_ = renderer;
    load_logo(renderer_);
    const float density = SDL_GetWindowPixelDensity(window_);
    if (density > 0.0f) SDL_SetRenderScale(renderer, density, density);
    return true;
}

void Ui::update_scale() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    const float fit = std::min(std::max(1, width) / 800.0f,
                               std::max(1, height) / 480.0f);
    const float limit = std::max(1.0f, std::min(4.0f, width / 360.0f));
    // Display scale includes pixel density; renderer scaling already handles it.
    const float density = std::max(1.0f, SDL_GetWindowPixelDensity(window_));
    const float logical_display_scale = SDL_GetWindowDisplayScale(window_) / density;
    const float scale = std::clamp(std::max(fit, logical_display_scale),
                                    1.0f, limit);
    if (scale == scale_) return;
    scale_ = scale;
    ImGui::GetStyle() = *base_style_;
    ImGui::GetStyle().ScaleAllSizes(scale_);
    ImGui::GetIO().FontGlobalScale = scale_;
}

void Ui::process_event(const SDL_Event& event) {
    if (!initialised_) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_FINGER_DOWN ||
        event.type == SDL_EVENT_FINGER_MOTION ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        // A touch anywhere wakes the unobtrusive in-game Menu control.  This
        // runs before the on-screen pad consumes the event, so pressing a game
        // button also counts as activity without stealing that press.
        wake_menu_button();
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.scancode == SDL_SCANCODE_ESCAPE ||
         event.key.scancode == SDL_SCANCODE_AC_BACK)) {
        menu_requested_ = true;
    }
}

// Use the panel under the initial touch for the entire gesture. The SDL
// backend labels touch-generated mouse events, so taps still use the normal
// widgets while a vertical drag cancels their pending click before release.
void Ui::update_touch_scroll() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseSource != ImGuiMouseSource_TouchScreen) {
        scroll_window_ = 0;
        scroll_dragging_ = false;
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGuiWindow* window = GImGui->HoveredWindow;
        while (window && (window->ScrollMax.y <= 0.0f ||
                         (window->Flags & ImGuiWindowFlags_NoScrollWithMouse))) {
            window = (window->Flags & ImGuiWindowFlags_ChildWindow)
                         ? window->ParentWindow : nullptr;
        }
        scroll_window_ = window ? window->ID : 0;
        scroll_start_y_ = io.MousePos.y;
        scroll_start_offset_ = window ? window->Scroll.y : 0.0f;
        scroll_dragging_ = false;
    }
    ImGuiWindow* window = ImGui::FindWindowByID(scroll_window_);
    if (window && window->WasActive) {
        const float distance = scroll_start_y_ - io.MousePos.y;
        if (io.MouseDown[0] && std::abs(distance) > 8.0f * scale_)
            scroll_dragging_ = true;
        if (scroll_dragging_) {
            ImGui::ClearActiveID();
            ImGui::SetScrollY(window, std::clamp(scroll_start_offset_ + distance,
                                               0.0f, window->ScrollMax.y));
        }
    }
    if (!io.MouseDown[0]) {
        scroll_window_ = 0;
        scroll_dragging_ = false;
    }
}

bool Ui::wants_mouse() const {
    return initialised_ && ImGui::GetIO().WantCaptureMouse;
}

bool Ui::wants_keyboard() const {
    return initialised_ && ImGui::GetIO().WantCaptureKeyboard;
}

// A notched or islanded handheld reserves screen edges the system draws over.
// In landscape the cutout sits on a long edge, directly over the navigation
// rail, so without this the first characters of the page are hidden behind it.
//
// The launcher already positions everything from the viewport's work area, so
// narrowing the work area to the safe area moves the whole UI at once and
// leaves the game picture underneath drawn full-bleed, which is what it should
// be. On a device with no insets SDL reports the whole window and this is a
// no-op.
void Ui::apply_safe_area() {
    if (window_ == nullptr) return;

    SDL_Rect safe{};
    if (!SDL_GetWindowSafeArea(window_, &safe)) return;
    if (safe.w <= 0 || safe.h <= 0) return;

    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(window_, &window_w, &window_h);
    if (window_w <= 0 || window_h <= 0) return;

    // SDL reports the safe area in window coordinates and ImGui's viewport is
    // in the same space, so the insets carry across directly.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) return;

    const float left = static_cast<float>(safe.x);
    const float top = static_cast<float>(safe.y);
    const float right = static_cast<float>(window_w - (safe.x + safe.w));
    const float bottom = static_cast<float>(window_h - (safe.y + safe.h));

    viewport->WorkPos = ImVec2(viewport->Pos.x + left, viewport->Pos.y + top);
    viewport->WorkSize = ImVec2(viewport->Size.x - left - right,
                                viewport->Size.y - top - bottom);
}


void Ui::touch_area(ImVec2* pos, ImVec2* size) const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    *pos = viewport->WorkPos;
    *size = viewport->WorkSize;
    if (viewport->Size.y <= viewport->Size.x) return;
    // Mirror App::present's portrait placement, in window points.
    const float margin =
        bezel_ ? std::min(viewport->Size.x, viewport->Size.y) * 0.035f : 0.0f;
    const float aspect = widescreen_ ? 16.0f / 9.0f : 4.0f / 3.0f;
    const float picture_bottom = viewport->WorkPos.y + margin * 2.0f +
                                 (viewport->Size.x - margin * 2.0f) / aspect;
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y;
    if (bottom - picture_bottom < viewport->WorkSize.y * 0.25f) return;
    pos->y = picture_bottom;
    size->y = bottom - picture_bottom;
}

UiIntent Ui::build(retro3do_t* emu, bool emulating, double display_fps,
                   double emulated_fps, double frame_ms, u64 underruns,
                   TouchPad& touch) {
    UiIntent intent;
    if (!initialised_) return intent;

    update_scale();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    apply_safe_area();
    update_touch_scroll();
    const bool touch_visible = touch.enabled();
    const bool touch_editing = touch.editing();
    bool touch_active = false;

    if (!splash_complete_) {
        draw_splash();
        if (SDL_GetTicks() - splash_started_ms_ >= 1400u ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            splash_complete_ = true;
        }
    } else if (!setup_complete_) {
        menu_requested_ = false;
        poll_setup_picker(intent);
        draw_setup_wizard(intent);
    } else if (touch_editing) {
        draw_pad_editor(touch, intent);
        touch_active = true;
    } else if (show_launcher_) {
        menu_requested_ = false;
        draw_launcher(emu, emulating, touch_visible, touch_editing, intent);
    } else {
        if (menu_requested_) {
            if (quick_menu_) {
                quick_menu_ = false;
                if (resume_after_menu_ && !emulating) intent.toggle_pause = true;
            } else {
                open_quick_menu(emulating, intent);
            }
            menu_requested_ = false;
        }
        draw_overlay(emu, emulating, display_fps, emulated_fps, frame_ms,
                     underruns, touch_visible, touch_editing, intent);
        touch_active = emulating && !quick_menu_ && !intent.toggle_pause;
        if (touch_active) {
            ImVec2 area_pos, area_size;
            touch_area(&area_pos, &area_size);
            touch.draw(area_pos, area_size);
        }
        if (quick_menu_) {
            draw_quick_menu(emu, emulating, touch_visible, touch_editing,
                            intent);
        }
    }

    touch.set_active(touch_active);
    ImGui::Render();
    return intent;
}

void Ui::draw_pad_editor(TouchPad& touch, UiIntent& intent) {
    const auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(viewport->Pos,
                        ImVec2(viewport->Pos.x + viewport->Size.x,
                               viewport->Pos.y + viewport->Size.y),
                        IM_COL32(11, 11, 14, 245));
    ImVec2 area_pos, area_size;
    touch_area(&area_pos, &area_size);
    touch.draw(area_pos, area_size);

    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
               viewport->WorkPos.y + 8.0f * scale_), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::Begin("##pad_editor_toolbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Drag controls to move them. Tap to customise.");
    if (ImGui::Button("DONE")) intent.toggle_layout_edit = true;
    ImGui::SameLine();
    if (ImGui::Button("RESET LAYOUT")) intent.reset_touch_layout = true;
    ImGui::End();

    if (menu_requested_ && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        intent.toggle_layout_edit = true;
    intent.touch_layout_changed = touch.draw_designer(menu_requested_);
    menu_requested_ = false;
}

void Ui::draw_splash() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##splash", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 top = viewport->Pos;
    const ImVec2 bottom(top.x + viewport->Size.x, top.y + viewport->Size.y);
    draw->AddRectFilledMultiColor(top, bottom, IM_COL32(18, 18, 24, 255),
                                  IM_COL32(18, 18, 24, 255),
                                  IM_COL32(6, 6, 9, 255),
                                  IM_COL32(6, 6, 9, 255));

    // The wordmark fades in, the bar grows under it, then the strapline.
    const float t = std::min(1.0f, (SDL_GetTicks() - splash_started_ms_) / 900.0f);
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);
    const float size = std::min({300.0f * scale_, viewport->Size.y * 0.45f,
                                 viewport->Size.x * 0.8f / wordmark_width(1.0f)});
    const float width = wordmark_width(size);
    const ImVec2 centre(top.x + viewport->Size.x * 0.5f,
                        top.y + viewport->Size.y * 0.44f);
    draw_wordmark(draw, ImVec2(centre.x - width * 0.5f, centre.y - size * 0.55f),
                  size, ease);
    const float bar_top = centre.y + size * 0.45f;

    const char* strap = "3DO INTERACTIVE MULTIPLAYER EMULATOR";
    const ImVec2 strap_size = ImGui::CalcTextSize(strap);
    ImGui::SetCursorScreenPos(ImVec2(centre.x - strap_size.x * 0.5f,
                                     bar_top + 20.0f * scale_));
    ImGui::TextColored(ImVec4(kMuted.x, kMuted.y, kMuted.z, ease), "%s", strap);
    ImGui::End();
}

void Ui::poll_setup_picker(UiIntent& intent) {
    if (!picking_games_folder_) return;
    const DocumentEntry picked = AndroidStorage::consume_picked_folder();
    if (picked.uri.empty()) return;

    intent.folder_path = picked.uri;
    intent.folder_name = picked.name;
    games_folder_name_ = picked.name;
    games_folder_target_ = picked.uri;
    intent.games_folder_chosen = true;
    picking_games_folder_ = false;
}

// First run asks for one thing only: where the games are. The built-in BIOS
// means there is nothing else to configure before playing.
void Ui::draw_setup_wizard(UiIntent& intent) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##setup_backdrop", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilledMultiColor(
        viewport->WorkPos,
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x,
               viewport->WorkPos.y + viewport->WorkSize.y),
        IM_COL32(18, 18, 24, 255), IM_COL32(18, 18, 24, 255),
        IM_COL32(6, 6, 9, 255), IM_COL32(6, 6, 9, 255));

    const float width = std::min(620.0f * scale_, viewport->WorkSize.x * 0.92f);
    const float height = std::min(400.0f * scale_, viewport->WorkSize.y * 0.92f);
    ImGui::SetCursorPos(ImVec2((viewport->WorkSize.x - width) * 0.5f,
                               (viewport->WorkSize.y - height) * 0.5f));
    ImGui::BeginChild("##setup_card", ImVec2(width, height),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushTextWrapPos(0.0f);

    // Brand header: wordmark and the tricolour rule.
    const float mark_size = 96.0f * scale_;
    const ImVec2 mark_at = ImGui::GetCursorScreenPos();
    draw_wordmark(ImGui::GetWindowDrawList(), mark_at, mark_size);
    ImGui::Dummy(ImVec2(wordmark_width(mark_size), mark_size));
    tricolour_rule(wordmark_width(mark_size), 4.0f * scale_);
    ImGui::Spacing();

    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Where are your games?");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(
        "Choose the folder that holds your 3DO games (ISO, BIN/CUE, IMG or "
        "CHD). Subfolders are scanned too, and a3DO can only see the folder "
        "you choose.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    const float button_height = 46.0f * scale_;
    ImGui::PushStyleColor(ImGuiCol_Button, kRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.26f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.12f, 1.0f));
    if (wide_button(games_folder_target_.empty() ? "CHOOSE GAMES FOLDER"
                                                 : "CHOOSE A DIFFERENT FOLDER",
                    button_height)) {
        picking_games_folder_ = true;
        AndroidStorage::pick_folder();
    }
    ImGui::PopStyleColor(3);

    if (picking_games_folder_) {
        ImGui::TextColored(kGold, "Waiting for the folder picker...");
    } else if (!games_folder_target_.empty()) {
        ImGui::TextColored(kGreen, "%s  -  %zu game%s found",
                           games_folder_name_.c_str(), library_.games().size(),
                           library_.games().size() == 1 ? "" : "s");
    } else {
        ImGui::TextColored(kMuted, "No folder chosen yet");
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(games_folder_target_.empty());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.22f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlue);
    if (wide_button("OPEN MY LIBRARY", button_height)) {
        setup_complete_ = true;
        show_launcher_ = true;
        page_ = Page::Library;
        intent.setup_finished = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextColored(kMuted,
                       "A built-in open-source BIOS (opera-boot) is ready. You "
                       "can add your own 3DO BIOS later in Settings.");

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::End();
}

void Ui::draw_launcher(retro3do_t* emu, bool emulating, bool touch_visible,
                       bool touch_editing, UiIntent& intent) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("a3DO", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    // Wide screens: the wordmark sits at the left of one row of buttons.
    // Narrow (portrait) screens: the wordmark shares the first row with the
    // RESUME and EXIT actions, and the page tabs get a full-width row of
    // their own beneath it.
    const ImVec2 bar_top = ImGui::GetCursorScreenPos();
    const float full_width = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float row_gap = ImGui::GetStyle().ItemSpacing.y;
    const float button_h = 34.0f * scale_;
    const float mark_size = 50.0f * scale_;
    const float mark_width = wordmark_width(mark_size) + 14.0f * scale_;
    // Buttons sit vertically centred on the logo's row.
    const float row_h = std::max(button_h, mark_size);
    const float button_y = bar_top.y + (row_h - button_h) * 0.5f;
    const int page_count = 4 + (retro_media_admin_ ? 1 : 0);
    // Apple's guidance is that an iOS app is never quit by the app itself, and
    // a visible quit control is a known review rejection. The system gesture is
    // the way out of a handheld; on a desktop the button still is.
    const int action_count = (RETRO3DO_PLATFORM_IOS ? 0 : 1) +
                             (session_available_ ? 1 : 0);
    const float wide_button = 92.0f * scale_;
    const bool narrow = full_width < mark_width +
        (page_count + action_count) * (wide_button + gap);
    const float action_width = 78.0f * scale_;

    draw_wordmark(ImGui::GetWindowDrawList(),
                  ImVec2(bar_top.x, bar_top.y + (row_h - mark_size) * 0.5f),
                  mark_size);

    float tab_x = bar_top.x + mark_width;
    float tab_y = button_y;
    float tab_width = 0.0f;
    float actions_x = 0.0f;
    float bar_height = row_h;
    if (narrow) {
        // Actions right-aligned on the wordmark row; tabs across the next.
        actions_x = bar_top.x + full_width -
                    action_count * action_width - (action_count - 1) * gap;
        tab_x = bar_top.x;
        tab_y = bar_top.y + row_h + row_gap;
        tab_width = (full_width - gap * (page_count - 1)) / page_count;
        bar_height = row_h + button_h + row_gap;
    } else {
        const float row = full_width - mark_width;
        tab_width = (row - gap * (page_count + action_count - 1)) /
                    (page_count + action_count);
        actions_x = tab_x + page_count * (tab_width + gap);
    }

    float cursor_x = tab_x;
    auto nav_button = [&](const char* label, Page page) {
        ImGui::SetCursorScreenPos(ImVec2(cursor_x, tab_y));
        cursor_x += tab_width + gap;
        const bool selected = page_ == page;
        // Pages cycle through the three theme colours.
        ImVec4 accent = kRed;
        if (page == Page::Settings || page == Page::System) accent = kGold;
        if (page == Page::About || page == Page::Downloads) accent = kBlue;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, tinted(accent, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  tinted(accent, 0.42f));
            ImGui::PushStyleColor(ImGuiCol_Text, kText);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        }
        if (ImGui::Button(label, ImVec2(tab_width, button_h))) page_ = page;
        if (selected) {
            // An accent underline marks the current page.
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(min.x + 8.0f * scale_, max.y - 3.0f * scale_),
                ImVec2(max.x - 8.0f * scale_, max.y), colour(accent),
                2.0f * scale_);
        }
        ImGui::PopStyleColor(selected ? 3 : 1);
    };
    nav_button("LIBRARY", Page::Library);
    nav_button("SETTINGS", Page::Settings);
    if (retro_media_admin_) {
        nav_button("DOWNLOADS", Page::Downloads);
    }
    nav_button("SYSTEM", Page::System);
    nav_button("ABOUT", Page::About);

    const float each_action = narrow ? action_width : tab_width;
    float action_cursor = actions_x;
    if (session_available_) {
        ImGui::SetCursorScreenPos(ImVec2(action_cursor, button_y));
        action_cursor += each_action + gap;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.36f, 0.22f, 1.0f));
        if (ImGui::Button("RESUME", ImVec2(each_action, button_h))) {
            show_launcher_ = false;
            wake_menu_button();
            if (!emulating) intent.toggle_pause = true;
        }
        ImGui::PopStyleColor();
    }
#if !RETRO3DO_PLATFORM_IOS
    ImGui::SetCursorScreenPos(ImVec2(action_cursor, button_y));
    if (ImGui::Button("EXIT", ImVec2(each_action, button_h))) {
        intent.quit = true;
    }
#endif
    // Full-width tricolour rule under the bar.
    ImGui::SetCursorScreenPos(ImVec2(bar_top.x, bar_top.y + bar_height + row_gap));
    tricolour_rule(full_width, 3.0f * scale_);
    ImGui::Spacing();
    const ImGuiWindowFlags page_flags = ImGuiWindowFlags_NoScrollbar;
    if (page_ != drawn_page_) {
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        drawn_page_ = page_;
    }
    ImGui::BeginChild("##page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      page_flags);
    switch (page_) {
        case Page::Library:  draw_library(emu, intent); break;
        case Page::Settings: draw_settings(emu, touch_visible, touch_editing,
                                           intent); break;
        case Page::Downloads: draw_downloads(intent); break;
        case Page::System:   draw_system(emu, intent); break;
        case Page::About:    draw_about(); break;
    }
    ImGui::EndChild();
    ImGui::End();

    draw_file_browser(intent);
}

void Ui::draw_library(retro3do_t* emu, UiIntent& intent) {

    const float rescan_width = 165.0f * scale_;
    const bool narrow_search = ImGui::GetContentRegionAvail().x < 420.0f * scale_;
    ImGui::SetNextItemWidth(narrow_search ? -1.0f :
        ImGui::GetContentRegionAvail().x - rescan_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##search", "Search your library...", search_buffer_,
                             sizeof(search_buffer_));
    if (!narrow_search) ImGui::SameLine();
    ImGui::BeginDisabled(games_folder_target_.empty());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.22f, 0.45f, 1.0f));
    if (ImGui::Button("RESCAN GAMES", ImVec2(narrow_search ? -1.0f : rescan_width, 0.0f))) {
        intent.rescan_games = true;
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    // The controls above stay fixed. Only the cards are a scrolling surface,
    // and the scrollbar itself is intentionally hidden: touch users swipe the
    // list, while desktop users can still use a wheel or trackpad.
    ImGui::BeginChild("##game_list", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    const std::vector<const LibraryGame*> games =
        library_.filtered(search_buffer_, library_group_);
    if (games.empty()) {
        ImGui::BeginChild("##empty_library", ImVec2(0.0f, 180.0f * scale_),
                          ImGuiChildFlags_Borders);
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextColored(kMuted, library_.games().empty()
                                      ? "YOUR LIBRARY IS EMPTY"
                                      : "NO GAMES MATCH THIS FILTER");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextWrapped(library_.games().empty()
                               ? "Put ISO, BIN/CUE, IMG or CHD images in your configured "
                                 "games folder, then choose Rescan Games."
                               : "Try another letter or clear the search box.");
        ImGui::EndChild();
        ImGui::EndChild();
        return;
    }

    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = available >= 720.0f * scale_
                            ? 4
                            : (available >= 540.0f * scale_
                                   ? 3
                                   : (available >= 400.0f * scale_ ? 2 : 1));
    const float card_height = 150.0f * scale_;

    // Nested child windows and table cells both introduce their own clip
    // channels. On Android's high-DPI SDL renderer those channels can lag a
    // row behind, leaving borders visible but clipping the card contents. One
    // dummy reserves the whole scrollable canvas; cards and buttons are then
    // placed directly in that single canvas with no nested clip state.
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float card_width = (available - gap * (columns - 1)) / columns;
    const size_t row_count =
        (games.size() + static_cast<size_t>(columns) - 1) /
        static_cast<size_t>(columns);
    const float grid_height = card_height * static_cast<float>(row_count) +
                              gap * static_cast<float>(row_count - 1);
    const ImVec2 grid_top = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(available, grid_height));
    const ImVec2 grid_end = ImGui::GetCursorScreenPos();
    const ImVec2 list_clip_min = ImGui::GetWindowPos();
    const ImVec2 list_clip_max(list_clip_min.x + ImGui::GetWindowWidth(),
                               list_clip_min.y + ImGui::GetWindowHeight());
    ImDrawList* const list_draw = ImGui::GetWindowDrawList();
    list_draw->PushClipRect(list_clip_min, list_clip_max, true);

    for (size_t index = 0; index < games.size(); ++index) {
            const LibraryGame& game = *games[index];
            const ImVec4 accent = card_accent(game.name);
            ImGui::PushID(game.target.c_str());

            const size_t row = index / static_cast<size_t>(columns);
            const size_t column = index % static_cast<size_t>(columns);
            const ImVec2 card_top(
                grid_top.x + static_cast<float>(column) * (card_width + gap),
                grid_top.y + static_cast<float>(row) * (card_height + gap));
            const ImVec2 card_bottom(card_top.x + card_width,
                                     card_top.y + card_height);
            if (card_bottom.y < list_clip_min.y || card_top.y > list_clip_max.y) {
                ImGui::PopID();
                continue;
            }
            ImDrawList* draw = list_draw;
            draw->AddRectFilled(card_top, card_bottom,
                                colour(tinted(accent, 0.045f)),
                                ImGui::GetStyle().ChildRounding);
            draw->AddRect(card_top, card_bottom,
                          IM_COL32(static_cast<int>(accent.x * 132.0f),
                                   static_cast<int>(accent.y * 132.0f),
                                   static_cast<int>(accent.z * 132.0f), 224),
                          ImGui::GetStyle().ChildRounding, 0,
                          std::max(1.0f, scale_));
            draw->AddRectFilled(
                card_top, ImVec2(card_top.x + 4.0f * scale_, card_bottom.y),
                colour(accent), ImGui::GetStyle().ChildRounding,
                ImDrawFlags_RoundCornersLeft);

            const float padding = 10.0f * scale_;
            const float content_left = card_top.x + padding;
            const float content_top = card_top.y + padding;
            const float art_height = 76.0f * scale_;
            SDL_Texture* cover = artwork_texture(game.name);
            if (cover != nullptr) {
                const ArtworkTexture& item = artwork_.find(game.name)->second;
                const float aspect = static_cast<float>(item.width) /
                                     static_cast<float>(std::max(1, item.height));
                const float art_width = std::clamp(art_height * aspect,
                                                   62.0f * scale_,
                                                   108.0f * scale_);
                const float art_left =
                    card_top.x + (card_width - art_width) * 0.5f;
                draw->AddImage(
                    ImTextureRef(static_cast<ImTextureID>(
                        reinterpret_cast<intptr_t>(cover))),
                    ImVec2(art_left, content_top),
                    ImVec2(art_left + art_width, content_top + art_height));
            } else {
                const float tile_width = 76.0f * scale_;
                const float tile_left =
                    card_top.x + (card_width - tile_width) * 0.5f;
                draw->AddRectFilled(
                    ImVec2(tile_left, content_top),
                    ImVec2(tile_left + tile_width, content_top + art_height),
                    colour(tinted(accent, 0.16f)), 6.0f * scale_);
                const char initial[2] = {card_initial(game.name), '\0'};
                const float initial_size = ImGui::GetFontSize() * 1.7f;
                const ImVec2 initial_bounds = ImGui::GetFont()->CalcTextSizeA(
                    initial_size, FLT_MAX, 0.0f, initial);
                draw->AddText(
                    ImGui::GetFont(), initial_size,
                    ImVec2(tile_left +
                               (tile_width - initial_bounds.x) * 0.5f,
                           content_top +
                               (art_height - initial_bounds.y) * 0.5f),
                    colour(accent), initial);
            }

            const float text_width = card_width - padding * 2.0f;
            draw_card_title(draw, game.name,
                            ImVec2(content_left,
                                   content_top + art_height + 6.0f * scale_),
                            text_width, IM_COL32(232, 240, 247, 255),
                            ImGui::GetFontSize() * 0.92f);
            const float button_height = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(
                ImVec2(content_left,
                       card_bottom.y - padding - button_height));
            ImGui::BeginDisabled(!retro3do_bios_loaded(emu) || retro3do_demo_loaded(emu));
            ImGui::PushStyleColor(ImGuiCol_Button, tinted(accent, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  tinted(accent, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
            if (ImGui::Button("PLAY",
                              ImVec2(card_width - padding * 2.0f, 0.0f))) {
                intent.disc_chosen = true;
                intent.start_disc = true;
                intent.disc_path = game.target;
                intent.disc_name = game.name;
                active_discs_ = game.discs;
                active_disc_ = 0;
                session_available_ = true;
                show_launcher_ = false;
                wake_menu_button();
            }
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();

            if (game.discs.size() > 1) {
                const std::string badge = std::to_string(game.discs.size()) + " DISCS";
                const ImVec2 badge_size = ImGui::GetFont()->CalcTextSizeA(
                    ImGui::GetFontSize() * 0.72f, FLT_MAX, 0.0f, badge.c_str());
                const ImVec2 badge_min(card_bottom.x - badge_size.x -
                                           16.0f * scale_,
                                       card_top.y + 7.0f * scale_);
                draw->AddRectFilled(
                    ImVec2(badge_min.x - 5.0f * scale_, badge_min.y - 2.0f * scale_),
                    ImVec2(card_bottom.x - 6.0f * scale_,
                           badge_min.y + badge_size.y + 2.0f * scale_),
                    IM_COL32(14, 14, 18, 230), 4.0f * scale_);
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.72f,
                              badge_min, colour(kGold), badge.c_str());
            }
            ImGui::PopID();
    }
    list_draw->PopClipRect();
    ImGui::SetCursorScreenPos(grid_end);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    if (!retro3do_bios_loaded(emu) || retro3do_demo_loaded(emu)) {
        ImGui::Spacing();
        ImGui::TextColored(
            kAmber,
            retro3do_demo_loaded(emu)
                ? "The hardware demo cannot boot games. Restart a3DO to return to the BIOS."
                : "No BIOS is loaded. Choose one in Settings.");
    }
    ImGui::EndChild();
}

void Ui::draw_settings(retro3do_t* emu, bool touch_visible, bool touch_editing,
                       UiIntent& intent) {
    page_heading("SETTINGS", "BIOS, controls and machine timing");

    draw_bios_panel(intent);
    ImGui::Spacing();

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const bool stacked = available_width < 700.0f * scale_;
    const float column_width = stacked ? available_width : (available_width - gap) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(10.0f * scale_, 10.0f * scale_));

    begin_panel("##controls_settings", column_width, stacked);
    ImGui::TextColored(kBlue, "ON-SCREEN CONTROLLER");
    ImGui::TextColored(kMuted, "Automatically hidden while a physical pad is connected.");
    if (ImGui::Button(touch_visible ? "ON - HIDE" : "OFF - SHOW",
                      ImVec2(165.0f * scale_, 0.0f))) {
        intent.toggle_touch_controls = true;
    }
    if (touch_visible) {
        same_line_if_fits(touch_editing ? "DONE MOVING" : "EDIT LAYOUT");
        if (ImGui::Button(touch_editing ? "DONE MOVING" : "EDIT LAYOUT")) {
            intent.toggle_layout_edit = true;
        }
        same_line_if_fits("RESET");
        if (ImGui::Button("RESET")) intent.reset_touch_layout = true;
    }

    ImGui::Separator();
    ImGui::TextColored(kGreen, "VIDEO STANDARD");
    int region = retro3do_get_region(emu) == RETRO3DO_REGION_PAL ? 1 : 0;
    if (ImGui::RadioButton("NTSC 60 Hz", &region, 0)) {
        intent.region_changed = true;
        intent.set_region_pal = false;
    }
    same_line_if_fits("PAL 50 Hz");
    if (ImGui::RadioButton("PAL 50 Hz", &region, 1)) {
        intent.region_changed = true;
        intent.set_region_pal = true;
    }
    end_panel();

    if (!stacked) ImGui::SameLine();
    begin_panel("##machine_settings", column_width, stacked);
    ImGui::TextColored(kAmber, "ARM CPU BOOST");
    ImGui::TextColored(kMuted, "Game CPU only; audio, video and FMV keep native timing.");
    int cpu = static_cast<int>(retro3do_get_cpu_scale_percent(emu));
    const int speeds[] = {100, 125, 150};
    const char* speed_names[] = {"100%", "125%", "150%"};
    for (int i = 0; i < 3; ++i) {
        if (i != 0) same_line_if_fits(speed_names[i]);
        if (ImGui::RadioButton(speed_names[i], &cpu, speeds[i])) {
            intent.cpu_scale_changed = true;
            intent.cpu_scale_percent = static_cast<u32>(speeds[i]);
        }
    }

    ImGui::Separator();
    ImGui::TextColored(kRed, "PERFORMANCE DISPLAY");
    if (ImGui::Checkbox("Show in-game performance HUD", &performance_visible_)) {
        intent.ui_settings_changed = true;
    }
    end_panel();
    ImGui::PopStyleVar();
}

// How finely the CEL engine draws. 1x is the machine's own picture; above
// that, polygons and scaled sprites are drawn on a finer grid while video the
// CPU draws itself stays as it is. Each step costs scale-squared more drawing.
void Ui::draw_render_scale(retro3do_t* emu, UiIntent& intent, bool compact) {
    ImGui::TextColored(compact ? kGold : kBlue, "RESOLUTION");
    int scale = static_cast<int>(retro3do_get_render_scale(emu));
    static const char* const kLabels[] = {"NATIVE", "2X", "3X", "4X", "5X", "6X"};
    static const char* const kIds[] = {"NATIVE##rs", "2X##rs", "3X##rs",
                                       "4X##rs", "5X##rs", "6X##rs"};
    for (int i = 0; i < RETRO3DO_MAX_RENDER_SCALE; ++i) {
        if (i != 0) same_line_if_fits(kLabels[i]);
        if (ImGui::RadioButton(kIds[i], &scale, i + 1)) {
            intent.render_scale_changed = true;
            intent.render_scale = static_cast<u32>(i + 1);
        }
    }
    if (!compact) {
        ImGui::TextColored(kMuted,
                           "Sharper 3D and scaled sprites. FMV stays native. "
                           "Higher steps need a faster device.");
    } else if (scale > 1) {
        ImGui::TextColored(kMuted, "%dx%d", 320 * scale,
                           (retro3do_get_region(emu) == RETRO3DO_REGION_PAL ? 288 : 240) * scale);
    }
}

// The BIOS choice. The built-in opera-boot needs no files; a real BIOS is an
// option for anyone who has one, and can be put back to built-in at any time.
void Ui::draw_bios_panel(UiIntent& intent) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(10.0f * scale_, 10.0f * scale_));
    begin_panel("##bios_settings", ImGui::GetContentRegionAvail().x, true);
    ImGui::TextColored(kRed, "SYSTEM BIOS");

    // Status pill: which ROM the machine is running.
    const char* tag = bios_custom_ ? "YOUR BIOS" : "BUILT-IN";
    const ImVec4 tag_colour = bios_custom_ ? kGold : kBlue;
    const ImVec2 tag_size = ImGui::CalcTextSize(tag);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 pad(8.0f * scale_, 3.0f * scale_);
    ImGui::GetWindowDrawList()->AddRectFilled(
        at, ImVec2(at.x + tag_size.x + pad.x * 2.0f, at.y + tag_size.y + pad.y * 2.0f),
        colour(tinted(tag_colour, 0.40f)), 50.0f);
    ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + pad.x, at.y + pad.y),
                                        colour(kText), tag);
    ImGui::Dummy(ImVec2(tag_size.x + pad.x * 2.0f, tag_size.y + pad.y * 2.0f));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad.y);
    ImGui::TextUnformatted(bios_name_.empty() ? "None" : bios_name_.c_str());

    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(
        bios_custom_
            ? "a3DO is using the BIOS you supplied. Switch back to the built-in "
              "BIOS at any time."
            : "opera-boot is an open-source boot ROM that starts the operating "
              "system on the game disc, so no BIOS file is needed. If you own a "
              "retail 3DO BIOS you can use it instead.");
    ImGui::PopStyleColor();

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float row = ImGui::GetContentRegionAvail().x;
    const bool two_buttons = bios_custom_;
    const float button_width =
        row >= 460.0f * scale_ && two_buttons ? (row - gap) * 0.5f : -1.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, tinted(kGold, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tinted(kGold, 0.40f));
    if (ImGui::Button(bios_custom_ ? "CHOOSE A DIFFERENT BIOS..."
                                   : "USE MY OWN BIOS...",
                      ImVec2(button_width, 40.0f * scale_))) {
        if (AndroidStorage::available()) {
            intent.pick_bios = true;
            bios_message_ = "Choose a 3DO BIOS file (.bin or .rom)";
        } else {
            browsing_ = Browsing::Bios;
            browser_.open("Choose a BIOS image", {".rom", ".bin"});
        }
    }
    ImGui::PopStyleColor(2);
    if (two_buttons) {
        if (button_width > 0.0f) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, tinted(kBlue, 0.24f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tinted(kBlue, 0.40f));
        if (ImGui::Button("USE BUILT-IN BIOS", ImVec2(button_width, 40.0f * scale_))) {
            intent.use_builtin_bios = true;
        }
        ImGui::PopStyleColor(2);
    }
    if (!bios_message_.empty()) ImGui::TextColored(kAmber, "%s", bios_message_.c_str());
    if (session_available_) {
        ImGui::TextColored(kMuted, "Changing the BIOS ends the game in progress.");
    }
    end_panel();
    ImGui::PopStyleVar();
}

void Ui::draw_downloads(UiIntent& intent) {
    page_heading("GAME DOWNLOADS", "Administrator RetroMedia catalogue");
    if (!retro_media_admin_) {
        ImGui::TextColored(kAmber,
                           "This page requires a RetroMedia administrator account.");
        return;
    }
    alphabet_filter(retro_media_group_, kAmber, scale_);

    const bool narrow_search = ImGui::GetContentRegionAvail().x < 420.0f * scale_;
    ImGui::SetNextItemWidth(narrow_search ? -1.0f :
        ImGui::GetContentRegionAvail().x - 180.0f * scale_ - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##rm_catalogue_search", "Search available games...",
                             retro_media_search_buffer_,
                             sizeof(retro_media_search_buffer_));
    if (!narrow_search) ImGui::SameLine();
    ImGui::BeginDisabled(retro_media_busy_);
    if (ImGui::Button("BROWSE / REFRESH", ImVec2(-1.0f, 0.0f))) {
        retro_media_busy_ = true;
        retro_media_message_ = "Loading administrator catalogue...";
        intent.retro_media_catalogue = true;
        intent.retro_media_search = retro_media_search_buffer_;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextColored(retro_media_busy_ ? kAmber : kMuted, "%s",
                       retro_media_message_.c_str());
    ImGui::BeginChild("##rm_game_catalogue", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (retro_media_catalogue_.empty()) {
        ImGui::TextWrapped(
            "Choose Browse / Refresh to list ROM sets available to this "
            "administrator. Downloads go to the selected Games folder.");
    }
    std::vector<const CatalogueItem*> shown;
    for (const CatalogueItem& game : retro_media_catalogue_) {
        char head = 0;
        for (unsigned char c : game.name) {
            if (std::isdigit(c)) { head = '#'; break; }
            if (std::isalpha(c)) {
                head = static_cast<char>(std::toupper(c));
                break;
            }
        }
        if (retro_media_group_ == 0 || head == retro_media_group_) {
            shown.push_back(&game);
        }
    }
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = available >= 720.0f * scale_ ? 4
                        : available >= 500.0f * scale_ ? 3
                        : available >= 340.0f * scale_ ? 2 : 1;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = (available - gap * (columns - 1)) / columns;
    float row_height = 0.0f;
    for (size_t index = 0; index < shown.size(); ++index) {
        const CatalogueItem& game = *shown[index];
        const ImVec4 accent = card_accent(game.name);
        if (index % static_cast<size_t>(columns) == 0) {
            float title_height = 0.0f;
            for (size_t i = index; i < shown.size() && i < index + columns; ++i)
                title_height = std::max(title_height, ImGui::CalcTextSize(
                    shown[i]->name.c_str(), nullptr, false,
                    std::max(1.0f, width - ImGui::GetStyle().WindowPadding.x * 2.0f)).y);
            row_height = std::max(126.0f * scale_, title_height + ImGui::GetTextLineHeight() +
                ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f +
                ImGui::GetStyle().ItemSpacing.y * 2.0f);
        }
        if (index % static_cast<size_t>(columns) != 0) ImGui::SameLine();
        ImGui::PushID(game.slug.c_str());
        ImGui::PushStyleColor(ImGuiCol_Border, tinted(accent, 0.45f));
        ImGui::BeginChild("##rm_game", ImVec2(width, row_height),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextWrapped("%s", game.name.c_str());
        const double mib = static_cast<double>(game.total_bytes) / (1024.0 * 1024.0);
        ImGui::TextColored(kMuted, "%d file%s  |  %.1f MiB", game.rom_files,
                           game.rom_files == 1 ? "" : "s", mib);
        ImGui::SetCursorPosY(row_height - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight());
        ImGui::PushStyleColor(ImGuiCol_Button, tinted(accent, 0.20f));
        ImGui::BeginDisabled(retro_media_busy_ || games_folder_target_.empty());
        if (ImGui::Button("DOWNLOAD", ImVec2(-1.0f, 0.0f))) {
            retro_media_busy_ = true;
            retro_media_message_ = "Downloading " + game.name + "...";
            intent.retro_media_download = true;
            intent.retro_media_slug = game.slug;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Ui::draw_system(retro3do_t* emu, UiIntent& intent) {
    page_heading("SYSTEM", "Renderer, display, disc and games folder");

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const bool stacked = available_width < 700.0f * scale_;
    const float column_width = stacked ? available_width : (available_width - gap) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(10.0f * scale_, 10.0f * scale_));

    begin_panel("##graphics_system", column_width, stacked);
    ImGui::TextColored(kBlue, "RENDERER");
    int renderer = renderer_backend_;
    if (ImGui::RadioButton("AUTO", &renderer, 0)) {
        renderer_backend_ = 0;
        renderer_startup_message_ = "Renderer change applies next launch";
        intent.ui_settings_changed = true;
    }
    same_line_if_fits("VULKAN");
    if (ImGui::RadioButton("VULKAN", &renderer, 1)) {
        renderer_backend_ = 1;
        renderer_startup_message_ = "Renderer change applies next launch";
        intent.ui_settings_changed = true;
    }
    ImGui::TextColored(kMuted, "Active: %s",
                       actual_renderer_.empty() ? "detecting" : actual_renderer_.c_str());
    if (!renderer_startup_message_.empty()) {
        ImGui::TextColored(kAmber, "%s", renderer_startup_message_.c_str());
    }

    ImGui::Separator();
    ImGui::TextColored(kBlue, "CUSTOM ADRENO / TURNIP DRIVER");
    ImGui::TextColored(gpu_driver_name_.empty() ? kMuted : kGreen, "%s",
                       gpu_driver_name_.empty() ? "System GPU driver"
                                                : gpu_driver_name_.c_str());
    if (AndroidStorage::available()) {
        if (ImGui::Button("IMPORT DRIVER PACKAGE", ImVec2(-1.0f, 0.0f))) {
            intent.import_gpu_driver = true;
        }
        if (!gpu_driver_name_.empty()) {
            if (ImGui::Button("USE SYSTEM DRIVER", ImVec2(-1.0f, 0.0f))) {
                intent.use_system_gpu_driver = true;
            }
        }
        ImGui::TextColored(kMuted,
                           "ADPKG/ZIP is copied to private app storage. Restart to apply.");
    } else {
        ImGui::TextColored(kMuted, "Custom mobile drivers are Android-only.");
    }
    if (!gpu_driver_message_.empty()) {
        ImGui::TextColored(kAmber, "%s", gpu_driver_message_.c_str());
    }

    ImGui::Separator();
    ImGui::TextColored(kRed, "DISPLAY");
    int aspect = widescreen_ ? 1 : 0;
    if (ImGui::RadioButton("4:3 ORIGINAL", &aspect, 0)) {
        widescreen_ = false;
        intent.ui_settings_changed = true;
    }
    same_line_if_fits("16:9 WIDE");
    if (ImGui::RadioButton("16:9 WIDE", &aspect, 1)) {
        widescreen_ = true;
        intent.ui_settings_changed = true;
    }
    if (ImGui::Checkbox("BEZEL", &bezel_)) intent.ui_settings_changed = true;
    same_line_if_fits("CRT EFFECT");
    if (ImGui::Checkbox("CRT EFFECT", &crt_effect_)) {
        intent.ui_settings_changed = true;
    }

    ImGui::Separator();
    draw_render_scale(emu, intent, false);
    end_panel();

    if (!stacked) ImGui::SameLine();
    begin_panel("##storage_system", column_width, stacked);
    ImGui::TextColored(kGreen, "INSERTED DISC");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##disc", "ISO, BIN/CUE, IMG or CHD",
                                 disc_path_buffer_, sizeof(disc_path_buffer_))) {
        disc_target_.clear();
    }
    if (ImGui::Button("BROWSE##disc", ImVec2(column_width * 0.48f, 0.0f))) {
        browsing_ = Browsing::Disc;
        browser_.open("Choose a disc image", {".iso", ".bin", ".cue", ".img", ".chd"});
    }
    ImGui::SameLine();
    if (ImGui::Button("INSERT##disc", ImVec2(-1.0f, 0.0f))) {
        intent.disc_chosen = true;
        intent.disc_path = disc_target_.empty() ? std::string(disc_path_buffer_)
                                                 : disc_target_;
        intent.disc_name = disc_path_buffer_;
    }
    if (retro3do_disc_loaded(emu)) {
        retro3do_disc_info disc;
        retro3do_get_disc_info(emu, &disc);
        ImGui::TextColored(kGreen, "Disc ready - %u sectors / %u track%s",
                           disc.sector_count, disc.track_count,
                           disc.track_count == 1 ? "" : "s");
        if (ImGui::Button("EJECT")) intent.eject = true;
        same_line_if_fits("START CURRENT DISC");
        ImGui::BeginDisabled(!retro3do_bios_loaded(emu) || retro3do_demo_loaded(emu));
        if (ImGui::Button("START CURRENT DISC")) {
            intent.reset = true;
            session_available_ = true;
            show_launcher_ = false;
            wake_menu_button();
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextColored(kMuted, "No disc inserted");
    }

    ImGui::Separator();
    ImGui::TextColored(kBlue, "GAMES FOLDER");
    ImGui::Text("%.48s", games_folder_name_.empty()
                             ? "Not configured" : games_folder_name_.c_str());
    if (AndroidStorage::available() &&
        ImGui::Button("CHANGE GAMES FOLDER", ImVec2(-1.0f, 0.0f))) {
        setup_complete_ = false;
    }
    end_panel();
    ImGui::PopStyleVar();
}

void Ui::draw_about() {
    page_heading("ABOUT a3DO", "A mobile-first 3DO emulator");

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const bool stacked = available_width < 700.0f * scale_;
    const float column_width = stacked ? available_width : (available_width - gap) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(10.0f * scale_, 10.0f * scale_));

    begin_panel("##about_story", column_width, stacked);
    {
        const float size = 120.0f * scale_;
        draw_wordmark(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), size);
        ImGui::Dummy(ImVec2(wordmark_width(size), size));
    }
    ImGui::SetWindowFontScale(0.88f);
    ImGui::TextColored(kMuted, "Version 2.1.3");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "a3DO is a 3DO emulator implementation, designed and "
        "built as its own modern C++ codebase for phones, handhelds, tablets "
        "and desktops. It is not a re-skinned legacy core.");
    ImGui::Spacing();
    ImGui::TextColored(kGreen, "BUILT FOR MODERN HARDWARE");
    ImGui::TextWrapped(
        "The ARM60 CPU uses a decoded-instruction cache, emulation runs on a "
        "dedicated paced thread, and rendering is presented independently. "
        "Android supports Vulkan, custom Adreno drivers, scoped storage and "
        "up to 120 Hz display presentation without changing game or audio speed.");
    ImGui::Spacing();
    ImGui::TextColored(kAmber, "ACTIVE DEVELOPMENT");
    ImGui::TextWrapped(
        "Commercial games including Road Rash and The Need for Speed reach "
        "correctly rendered gameplay. Compatibility continues to expand; "
        "save states, CD audio and less-used hardware paths remain future work.");
    end_panel();

    if (!stacked) ImGui::SameLine();
    begin_panel("##about_technology", column_width, stacked);
    ImGui::SetWindowFontScale(0.88f);
    ImGui::TextColored(kBlue, "THE EMULATED SYSTEM");
    ImGui::TextWrapped(
        "Project implementations cover the ARM60, CLIO timing and interrupts, "
        "MADAM/CEL graphics and matrix hardware, VDLP display lists, DSPP "
        "audio, SPORT memory operations, XBUS CD-ROM and chained controllers.");
    ImGui::Spacing();
    ImGui::TextColored(kGold, "BUILT-IN BIOS");
    ImGui::TextWrapped(
        "opera-boot by trapexit, an open-source 3DO boot ROM licensed under "
        "GPL-2.0. Its complete source ships with a3DO's source code.");
    ImGui::Spacing();
    ImGui::TextColored(kGreen, "LICENCE & OWNERSHIP");
    ImGui::TextWrapped(
        "Application and emulation core: MIT License, Copyright (c) 2026 Crown "
        "Park Computing. Bundled components keep their own licences: SDL3, "
        "Dear ImGui, Roboto, AdrenoTools and opera-boot.");
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped(
        "Licence notices do not replace third-party rights clearance; see "
        "NOTICE and PROVENANCE.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::TextColored(kBlue, "DEVELOPED WITH EVIDENCE");
    ImGui::TextWrapped(
        "The core is backed by 250+ focused regression checks, commercial-game "
        "traces, official 3DO and ARM documentation, published hardware patents "
        "and device testing. Opera/FreeDO was also consulted historically as a "
        "compatibility reference; the project does not claim clean-room history.");
    ImGui::Spacing();
    ImGui::TextColored(kRed, "INCLUDED FEATURES");
    ImGui::TextWrapped(
        "Built-in BIOS | ISO/BIN/CUE/IMG/CHD | live multi-disc swap | gamepad "
        "and touch controls | portrait and landscape | shared mobile and desktop "
        "core");
    end_panel();
    ImGui::PopStyleVar();
}

void Ui::open_quick_menu(bool emulating, UiIntent& intent) {
    quick_menu_ = true;
    resume_after_menu_ = emulating;
    if (emulating) intent.toggle_pause = true;
}

void Ui::draw_overlay(retro3do_t* emu, bool emulating, double display_fps,
                      double emulated_fps, double frame_ms, u64 underruns,
                      bool touch_visible, bool touch_editing, UiIntent& intent) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr u64 kMenuVisibleMs = 3000;
    const bool show_menu_button =
        touch_editing ||
        SDL_GetTicks() - last_pointer_activity_ms_ < kMenuVisibleMs;
    if (show_menu_button && !quick_menu_) {
        // Top-centre is deliberately outside both shoulder-button hit areas.
        // The old top-left position covered L on handheld layouts.
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                   viewport->WorkPos.y + 12.0f * scale_),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);
        ImGui::Begin("##menu_button", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);
        if (ImGui::Button("MENU", ImVec2(92.0f * scale_, 0.0f))) {
            open_quick_menu(emulating, intent);
        }
        if (touch_editing) {
            ImGui::SameLine();
            if (ImGui::Button("DONE MOVING")) intent.toggle_layout_edit = true;
        }
        ImGui::End();
    }

    if (!performance_visible_) return;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 12.0f * scale_,
                                  viewport->WorkPos.y + 68.0f * scale_));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##performance", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoNav);
    const double target = retro3do_get_region(emu) == RETRO3DO_REGION_PAL ? 50.0 : 60.0;
    ImGui::TextColored(emulated_fps >= target - 2.0 ? kGreen : kAmber,
                       "Machine %.0f / %.0f fps", emulated_fps, target);
    ImGui::Text("Display %.0f fps   Frame %.2f ms", display_fps, frame_ms);
    retro3do_stats stats;
    retro3do_get_stats(emu, &stats);
    ImGui::Text("ARM %u%%   PC %08X", retro3do_get_cpu_scale_percent(emu),
                stats.cpu_pc);
    if (underruns > 0) {
        ImGui::TextColored(kRed, "Audio gaps %llu",
                           static_cast<unsigned long long>(underruns));
    }
    ImGui::End();
}

void Ui::draw_quick_menu(retro3do_t* emu, bool emulating, bool touch_visible,
                         bool touch_editing, UiIntent& intent) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos,
        ImVec2(viewport->Pos.x + viewport->Size.x,
               viewport->Pos.y + viewport->Size.y),
        IM_COL32(4, 4, 6, 190));

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                  viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    // Fixed width, height fitted to the contents up to the screen; anything
    // beyond that scrolls (by swipe, via update_touch_scroll) instead of
    // being cut off.
    const float menu_width = std::min(465.0f * scale_, viewport->WorkSize.x * 0.88f);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(menu_width, 0.0f),
        ImVec2(menu_width, viewport->WorkSize.y * 0.94f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f * scale_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("##quick_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(2);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(kText, "GAME MENU");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(kMuted, "Game paused - machine timing is preserved");
    tricolour_rule(ImGui::GetContentRegionAvail().x, 3.0f * scale_);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.36f, 0.22f, 1.0f));
    if (wide_button("RESUME", 42.0f * scale_)) {
        quick_menu_ = false;
        if (resume_after_menu_ && !emulating) intent.toggle_pause = true;
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::TextColored(kGold, "ON-SCREEN CONTROLLER");
    if (ImGui::Checkbox("Enabled", &touch_visible)) {
        intent.toggle_touch_controls = true;
    }
    same_line_if_fits(touch_editing ? "DONE MOVING" : "EDIT LAYOUT");
    ImGui::BeginDisabled(!touch_visible);
    if (ImGui::Button(touch_editing ? "DONE MOVING" : "EDIT LAYOUT")) {
        intent.toggle_layout_edit = true;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextColored(kGold, "ARM CPU BOOST");
    int cpu = static_cast<int>(retro3do_get_cpu_scale_percent(emu));
    const int values[] = {100, 125, 150};
    const char* labels[] = {"100%", "125%", "150%"};
    for (int i = 0; i < 3; ++i) {
        if (i != 0) same_line_if_fits(labels[i]);
        if (ImGui::RadioButton(labels[i], &cpu, values[i])) {
            intent.cpu_scale_changed = true;
            intent.cpu_scale_percent = static_cast<u32>(values[i]);
        }
    }
    ImGui::TextColored(kMuted, "Audio, video and FMV remain at native speed.");

    ImGui::Spacing();
    draw_render_scale(emu, intent, true);

    if (active_discs_.size() > 1) {
        ImGui::Spacing();
        ImGui::TextColored(kAmber, "DISC SELECT");
        for (size_t i = 0; i < active_discs_.size(); ++i) {
            const std::string label = "DISC " + std::to_string(i + 1);
            if (i != 0) same_line_if_fits(label.c_str());
            ImGui::BeginDisabled(i == active_disc_);
            if (ImGui::Button(label.c_str())) {
                intent.disc_chosen = true;
                intent.disc_path = active_discs_[i].target;
                intent.disc_name = active_discs_[i].name;
                active_disc_ = i;
            }
            ImGui::EndDisabled();
        }
        ImGui::TextColored(kMuted,
                           "Swap when the game asks; the 3DO is not reset.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    // The three ways out share one row so the menu fits a landscape phone.
    const int exits = RETRO3DO_PLATFORM_IOS ? 2 : 3;
    const float exit_gap = ImGui::GetStyle().ItemSpacing.x;
    const float exit_width =
        (ImGui::GetContentRegionAvail().x - exit_gap * (exits - 1)) / exits;
    if (ImGui::Button("RESET", ImVec2(exit_width, 40.0f * scale_))) {
        intent.reset = true;
        quick_menu_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("LIBRARY", ImVec2(exit_width, 40.0f * scale_))) {
        page_ = Page::Library;
        show_launcher_ = true;
        quick_menu_ = false;
    }
#if !RETRO3DO_PLATFORM_IOS
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, tinted(kRed, 0.25f));
    if (ImGui::Button("EXIT", ImVec2(exit_width, 40.0f * scale_))) intent.quit = true;
    ImGui::PopStyleColor();
#endif
    ImGui::PopTextWrapPos();
    ImGui::End();
}

void Ui::draw_file_browser(UiIntent& intent) {
    std::string picked;
    std::string picked_name;
    if (!browser_.draw(&picked, &picked_name)) return;

    if (browsing_ == Browsing::Bios) {
        intent.bios_chosen = true;
        intent.bios_path = picked;
        intent.bios_name = picked_name;
    } else if (browsing_ == Browsing::Disc) {
        std::snprintf(disc_path_buffer_, sizeof(disc_path_buffer_), "%s",
                      picked_name.empty() ? picked.c_str() : picked_name.c_str());
        disc_target_ = picked;
        intent.disc_chosen = true;
        intent.disc_path = picked;
        intent.disc_name = picked_name;
    }
    browsing_ = Browsing::None;
}

void Ui::release_ui_texture() {
    SDL_DestroyTexture(ui_texture_);
    ui_texture_ = nullptr;
    ui_texture_width_ = ui_texture_height_ = 0;
}

void Ui::render(SDL_Renderer* renderer) {
    if (!initialised_) return;
    const char* backend = SDL_GetRendererName(renderer);
    if (!backend || std::strcmp(backend, "vulkan") != 0) {
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        return;
    }

    // SDL's Vulkan swapchain scissor does not follow rotated Android surfaces.
    // Texture targets have identity rotation: clip the UI there, then composite
    // it in a single full-frame draw, preserving the game underneath overlays.
    int width = 0, height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    if (width != ui_texture_width_ || height != ui_texture_height_) {
        release_ui_texture();
    }
    if (!ui_texture_ && width > 0 && height > 0) {
        ui_texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_TARGET, width, height);
        if (ui_texture_) {
            ui_texture_width_ = width;
            ui_texture_height_ = height;
            // Drawing onto transparent pixels has already multiplied RGB by A.
            if (!SDL_SetTextureBlendMode(ui_texture_, SDL_BLENDMODE_BLEND_PREMULTIPLIED))
                release_ui_texture();
        }
    }
    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (!ui_texture_ || !SDL_SetRenderTarget(renderer, ui_texture_)) {
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        return;
    }
    SDL_FColor previous_colour;
    SDL_GetRenderDrawColorFloat(renderer, &previous_colour.r, &previous_colour.g,
                               &previous_colour.b, &previous_colour.a);
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderClipRect(renderer, nullptr);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_SetRenderTarget(renderer, previous_target);
    // Viewport, clip and scale are restored by SDL with the previous target.
    SDL_RenderTexture(renderer, ui_texture_, nullptr, nullptr);
    SDL_SetRenderDrawColorFloat(renderer, previous_colour.r, previous_colour.g,
                               previous_colour.b, previous_colour.a);
}

}  // namespace retro3do
