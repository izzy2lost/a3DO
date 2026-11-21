#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <string>

#include "app/settings.h"
#include "ui/touch_pad.h"
#include "test_harness.h"

using namespace retro3do;
namespace {
struct Input {
    std::map<retro3do_pad_button, bool> held;
    TouchPad pad{[this](retro3do_pad_button b, bool down) { held[b] = down; }};
    Input() { pad.set_visible(true); pad.set_active(true); }
    bool finger(Uint32 type, SDL_FingerID id, float x, float y, bool available = true) {
        SDL_Event e{}; e.type = type; e.tfinger.fingerID = id;
        e.tfinger.x = x / 1280; e.tfinger.y = y / 720;
        return pad.handle_event(e, 1280, 720, available);
    }
    bool any() const {
        for (const auto& b : held) if (b.second) return true;
        return false;
    }
    // The centre of a control, wherever the current layout puts it. A stray
    // mouse move makes the pad lay itself out for the window without
    // pressing anything.
    ImVec2 at(const char* id) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION;
        pad.handle_event(e, 1280, 720);
        ImVec2 lo, hi;
        CHECK(pad.control_bounds(id, &lo, &hi));
        return ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
    }
};
struct Scratch {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("retro3do-touch-" + std::to_string(SDL_GetPerformanceCounter()));
    Settings settings;
    Scratch() {
        std::filesystem::create_directory(dir);
        settings.load((dir / "settings.cfg").string());
    }
    ~Scratch() { std::filesystem::remove_all(dir); }
};
}

TEST(shared_pad_maps_all_seven_3do_buttons) {
    Input input;
    const struct { const char* id; retro3do_pad_button button; } buttons[] = {
        {"a", RETRO3DO_PAD_A}, {"b", RETRO3DO_PAD_B}, {"c", RETRO3DO_PAD_C},
        {"lshift", RETRO3DO_PAD_LEFT_SHIFT}, {"rshift", RETRO3DO_PAD_RIGHT_SHIFT},
        {"stop", RETRO3DO_PAD_STOP}, {"play", RETRO3DO_PAD_PLAY},
    };
    for (const auto& b : buttons) {
        const ImVec2 p = input.at(b.id);
        CHECK(input.finger(SDL_EVENT_FINGER_DOWN, 1, p.x, p.y));
        CHECK(input.held[b.button]);
        CHECK(input.finger(SDL_EVENT_FINGER_UP, 1, p.x, p.y));
        CHECK(!input.any());
    }
}

TEST(face_buttons_rise_a_b_c_like_the_real_pad) {
    // On the 3DO pad A is lowest on the left and C highest on the right.
    Input input;
    const ImVec2 a = input.at("a"), b = input.at("b"), c = input.at("c");
    CHECK(a.x < b.x); CHECK(b.x < c.x);
    CHECK(a.y > b.y); CHECK(b.y > c.y);
}

TEST(shared_pad_combines_diagonals_and_simultaneous_fire) {
    Input input;
    const ImVec2 dpad = input.at("dpad"), a = input.at("a");
    CHECK(input.finger(SDL_EVENT_FINGER_DOWN, 1, dpad.x - 80, dpad.y - 80));
    CHECK(input.held[RETRO3DO_PAD_UP]); CHECK(input.held[RETRO3DO_PAD_LEFT]);
    CHECK(input.finger(SDL_EVENT_FINGER_DOWN, 2, a.x, a.y));
    CHECK(input.held[RETRO3DO_PAD_A]);
    CHECK(input.finger(SDL_EVENT_FINGER_MOTION, 1, dpad.x + 80, dpad.y + 80));
    CHECK(!input.held[RETRO3DO_PAD_UP]); CHECK(!input.held[RETRO3DO_PAD_LEFT]);
    CHECK(input.held[RETRO3DO_PAD_DOWN]); CHECK(input.held[RETRO3DO_PAD_RIGHT]);
    CHECK(input.held[RETRO3DO_PAD_A]);
    input.pad.release_all();
    CHECK(!input.any());
}

TEST(shared_pad_keeps_a_button_held_until_both_fingers_lift) {
    Input input;
    const ImVec2 a = input.at("a");
    input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y);
    input.finger(SDL_EVENT_FINGER_DOWN, 2, a.x, a.y);
    input.finger(SDL_EVENT_FINGER_UP, 1, a.x, a.y);
    CHECK(input.held[RETRO3DO_PAD_A]);
    input.finger(SDL_EVENT_FINGER_UP, 2, a.x, a.y);
    CHECK(!input.any());
}

TEST(shared_pad_does_not_release_unrelated_keyboard_directions) {
    Input input;
    input.held[RETRO3DO_PAD_LEFT] = true;  // keyboard already holds left
    const ImVec2 dpad = input.at("dpad");
    input.finger(SDL_EVENT_FINGER_DOWN, 1, dpad.x, dpad.y - 100);
    CHECK(input.held[RETRO3DO_PAD_UP]);
    CHECK(input.held[RETRO3DO_PAD_LEFT]);
    input.pad.release_all();
    CHECK(!input.held[RETRO3DO_PAD_UP]);
    CHECK(input.held[RETRO3DO_PAD_LEFT]);
}

TEST(shared_pad_releases_on_menu_hide_controller_focus_and_edit) {
    for (int action = 0; action < 5; ++action) {
        Input input;
        const ImVec2 a = input.at("a"), dpad = input.at("dpad");
        input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y);
        input.finger(SDL_EVENT_FINGER_DOWN, 2, dpad.x, dpad.y - 100);
        CHECK(input.any());
        switch (action) {
        case 0: input.pad.set_active(false); break;
        case 1: input.pad.set_visible(false); break;
        case 2: input.pad.set_physical_gamepad_present(true); break;
        case 3: {
            SDL_Event e{}; e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            input.pad.handle_event(e, 1280, 720); break;
        }
        case 4: input.pad.set_editing(true); break;
        }
        CHECK(!input.any());
    }
}

TEST(shared_pad_handles_cancel_and_release_over_a_menu) {
    Input input;
    const ImVec2 a = input.at("a");
    CHECK(!input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y, false));
    CHECK(!input.any());
    input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y);
    CHECK(input.finger(SDL_EVENT_FINGER_UP, 1, a.x, a.y, false));
    CHECK(!input.any());
    input.finger(SDL_EVENT_FINGER_DOWN, 2, a.x, a.y);
    CHECK(input.finger(SDL_EVENT_FINGER_CANCELED, 2, a.x, a.y));
    CHECK(!input.any());
}

TEST(shared_pad_ignores_synthetic_mouse_and_restores_after_controller_disconnect) {
    Input input;
    const ImVec2 a = input.at("a");
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT; e.button.which = SDL_TOUCH_MOUSEID;
    e.button.x = a.x; e.button.y = a.y;
    CHECK(!input.pad.handle_event(e, 1280, 720));
    CHECK(!input.any());
    input.pad.set_physical_gamepad_present(true);
    CHECK(input.pad.enabled()); CHECK(!input.pad.visible());
    CHECK(!input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y));
    input.pad.set_physical_gamepad_present(false);
    CHECK(input.pad.visible());
    CHECK(input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y));
    CHECK(input.held[RETRO3DO_PAD_A]);
}

TEST(shared_pad_editor_drags_and_persists_without_game_input) {
    Scratch scratch;
    Input input;
    input.pad.load_layout(scratch.settings);
    const ImVec2 a = input.at("a");
    input.pad.set_editing(true);
    const auto before = input.pad.layout().clusters.at("face");
    input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y);
    input.finger(SDL_EVENT_FINGER_MOTION, 1, a.x - 128, a.y - 72);
    input.finger(SDL_EVENT_FINGER_UP, 1, a.x - 128, a.y - 72);
    CHECK(!input.any()); CHECK(input.pad.take_dirty());
    const auto moved = input.pad.layout().clusters.at("face");
    CHECK(std::fabs(moved.dx - (before.dx - .1f)) < .001f);
    CHECK(std::fabs(moved.dy - (before.dy - .1f)) < .001f);
    input.pad.save_layout(scratch.settings);
    CHECK(scratch.settings.save());
    TouchPad restored;
    restored.load_layout(scratch.settings);
    CHECK(restored.layout().encode() == input.pad.layout().encode());
}

TEST(shared_pad_restores_size_spacing_opacity_and_extra_buttons) {
    Scratch scratch;
    auto layout = touchpad::Layout::defaults(touchpad::profile_3do());
    layout.clusters["face"].scale = 1.7f;
    layout.clusters["face"].spacing = 1.4f;
    layout.opacity = .4f;
    touchpad::Extra extra; extra.id = "a"; extra.label = "A";
    extra.dx = .5f; extra.dy = .5f;
    layout.extras.push_back(extra);
    CHECK(layout.save(scratch.dir.string()));
    Input input;
    input.pad.load_layout(scratch.settings);
    CHECK(input.pad.layout().encode() == layout.encode());
    CHECK(input.finger(SDL_EVENT_FINGER_DOWN, 1, 640, 360));
    CHECK(input.held[RETRO3DO_PAD_A]);
}

TEST(old_individual_button_settings_start_with_the_new_grouped_pad) {
    Scratch scratch;
    scratch.settings.set("touch.a.x", "0.9381");
    scratch.settings.set("touch.a.y", "0.8150");
    scratch.settings.set("touch.a.r", "0.0550");
    Input input;
    input.pad.load_layout(scratch.settings);
    const ImVec2 a = input.at("a");
    CHECK(input.pad.layout().extras.empty());
    for (const auto& cluster : input.pad.layout().clusters) CHECK(cluster.second.visible);
    CHECK(input.finger(SDL_EVENT_FINGER_DOWN, 1, a.x, a.y));
    CHECK(input.held[RETRO3DO_PAD_A]);
}

namespace {
std::string retroid_legacy_layout() {
    const auto path = std::filesystem::path(__FILE__).parent_path() /
        "fixtures/legacy_touch_layout.json";
    std::ifstream file(path);
    CHECK(file.good());
    std::stringstream contents; contents << file.rdbuf();
    return contents.str();
}
}

TEST(retroid_legacy_extras_are_backed_up_and_replaced_only_once) {
    Scratch scratch;
    const std::string old = retroid_legacy_layout();
    const auto path = scratch.dir / "pad_layout_3do.json";
    { std::ofstream file(path); file << old; }
    Input input;
    input.pad.load_layout(scratch.settings);
    CHECK(input.pad.layout().extras.empty());
    for (const auto& cluster : input.pad.layout().clusters) CHECK(cluster.second.visible);
    const auto saved = touchpad::Layout::load(scratch.dir.string(), touchpad::profile_3do());
    CHECK(saved.encode() == input.pad.layout().encode());
    std::ifstream backup(path.string() + ".legacy");
    std::stringstream contents; contents << backup.rdbuf();
    CHECK(contents.str() == old);

    auto edited = saved;
    edited.clusters["face"].dx = .64f;
    CHECK(edited.save(scratch.dir.string()));
    input.pad.load_layout(scratch.settings);
    CHECK(input.pad.layout().encode() == edited.encode());
}

TEST(user_created_individual_shared_buttons_are_not_mistaken_for_legacy) {
    Scratch scratch;
    auto custom = touchpad::Layout::decode(retroid_legacy_layout(), touchpad::profile_3do());
    for (auto& button : custom.extras) {
        if (button.id == "play") button.label = "PLAY";
        if (button.id == "stop") button.label = "STOP";
    }
    CHECK(custom.save(scratch.dir.string()));
    Input input;
    input.pad.load_layout(scratch.settings);
    CHECK(input.pad.layout().encode() == custom.encode());
    CHECK(!std::filesystem::exists(scratch.dir / "pad_layout_3do.json.legacy"));
}

TEST(shared_pad_corrupt_layout_recovers_to_a_working_3do_profile) {
    const auto& profile = touchpad::profile_3do();
    for (const char* bad : {"{broken", "{\"profile\":\"saturn\"}"}) {
        const auto decoded = touchpad::Layout::decode(bad, profile);
        CHECK(decoded.encode() == touchpad::Layout::defaults(profile).encode());
    }
}

TEST(an_untouched_layout_from_an_earlier_build_takes_the_new_defaults) {
    Scratch scratch;
    const std::string old =
        "{\"version\":1,\"profile\":\"3do\",\"stick\":\"dpad\",\"opacity\":0.6,\"clusters\":{"
        "\"dpad\":{\"pos\":[0.14,0.73],\"scale\":1,\"spacing\":1,\"visible\":true},"
        "\"face\":{\"pos\":[0.82,0.73],\"scale\":1,\"spacing\":1,\"visible\":true},"
        "\"meta\":{\"pos\":[0.5,0.88],\"scale\":1,\"spacing\":1,\"visible\":true},"
        "\"shoulder_l\":{\"pos\":[0.12,0.16],\"scale\":1,\"spacing\":1,\"visible\":true},"
        "\"shoulder_r\":{\"pos\":[0.88,0.16],\"scale\":1,\"spacing\":1,\"visible\":true}},\"extras\":[]}";
    { std::ofstream file(scratch.dir / "pad_layout_3do.json"); file << old; }
    Input input;
    input.pad.load_layout(scratch.settings);
    const auto& face = input.pad.layout().clusters.at("face");
    CHECK(std::fabs(face.dx - 0.92f) < .001f);
    CHECK(std::fabs(input.pad.layout().opacity - 0.6f) < .001f);
    const auto saved = touchpad::Layout::load(scratch.dir.string(), touchpad::profile_3do());
    CHECK(saved.encode() == input.pad.layout().encode());

    // One group moved by the player: the whole layout is theirs and stays.
    auto edited = touchpad::Layout::decode(old, touchpad::profile_3do());
    edited.clusters["face"].dx = .70f;
    CHECK(edited.save(scratch.dir.string()));
    input.pad.load_layout(scratch.settings);
    CHECK(input.pad.layout().encode() == edited.encode());
}
