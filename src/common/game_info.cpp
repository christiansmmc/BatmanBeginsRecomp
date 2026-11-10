#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <exception>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/fps_policy.h"
#include "common/keyboard_pad.h"
#include "common/options_screen.h"
#include "common/pad_script.h"
#include "core/input.h"
#include "core/log.h"

// experimental_play_fps in the settings file, 60 or 0 for the game's own rate: PlayFrameRateChosen in src/game/hooks.cpp plays
// at the rate read at the start, and the options screen's choice is saved for the next
PlayRateSetting g_play_rate;

namespace {

// fps_cap as the file has it: Init limits the value in use, and saving a setting must not write that over the player's
uint32_t g_fps_cap_in_file = 0;

uint32_t MonitorRefreshHz() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    return EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ? mode.dmDisplayFrequency : 60;
}

void LoadMore(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    g_play_rate.Load(t["experimental_play_fps"].value_or(uint32_t{0}));
    options::LoadSettings(table);
}

// app::Save writes the whole file, and a key the player adds beside one already written makes a duplicate, which
// app::Load takes as a broken file. So the frame rate is only written when it is not the game's own.
void SaveMore(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    if (const uint32_t fps = g_play_rate.Chosen()) t.insert_or_assign("experimental_play_fps", int64_t(fps));
    t.insert_or_assign("fps_cap", int64_t(g_fps_cap_in_file));
    options::SaveSettings(table);
}

void Init() {
    app::Settings& settings = app::Get();
    g_fps_cap_in_file = settings.fps_cap;
    settings.fps_cap = EffectiveFpsCap(settings.fps_cap, MonitorRefreshHz(), kMaxValidatedFps);
    LOGI("fps cap %u", settings.fps_cap);
    try {
        if (LoadPadScriptFromEnv()) LOGI("pad script active");
    } catch (const std::exception& e) {
        LOGE("%s", e.what());
        std::terminate();
    }
}

void WindowUp(void*) { options::LoadFonts(); }

void Ui(double) {
    CountPresentedFrame();
    options::DrawScreen();
}

void Pad(int port, input::Pad& pad) {
    ApplyPadScript(port, pad);
    options::SeeController(port, pad);
    ApplyWindowKeyboard(port, pad);
    options::GateReport(port, pad);
}

bool Message(void*, uint32_t message, uintptr_t wp, intptr_t lp, intptr_t*) {
    KeyboardWindowMessage(message, wp, lp);
    options::OnWindowMessage(message, wp, lp);
    return false;
}

}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Batman Begins",            // window and splash
    .short_title = L"Batman Begins",      // message boxes
    .name = L"BatmanBegins",              // window classes, crash dumps, handover event
    .log = "batman",                      // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the US ISO of Batman Begins for Xbox in the assets folder, next to this program.",
    .other_release = L" is from another release of the game. This program only runs with the US ISO of Batman Begins for Xbox. "
                     L"Replace the file in the assets folder, next to this program, and open it again.",
    .device_lost = L"A graphics error occurred and the game will close. Anything after the last checkpoint was lost. "
                   L"If this happens again, send the file logs\\batman.log.",
    .images = {{"default.xbe", "BatmanBegins"}},  // each recompiled xbe and its exe, for title launches
    .options = {},                        // game settings kept in the settings file
    .load = LoadMore,                        // reads more from the settings file's toml::table
    .save = SaveMore,                        // writes more into it
    .init = Init,                         // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = WindowUp,                    // the game window is up
    .message = Message,                   // window messages first; true takes one
    .ui = Ui,                             // ImGui drawing each frame
    .pad = Pad,                           // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
