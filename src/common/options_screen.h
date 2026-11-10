// The options screen, drawn over the game with ImGui, and the notice of hold to skip on a video. See docs/TECHNICAL.md,
// "Options screen".
#pragma once
#include <cstdint>

namespace input {
struct Pad;
}

namespace options {

void LoadFonts();    // GameInfo::window
void DrawScreen();   // GameInfo::ui
void OnWindowMessage(uint32_t message, uintptr_t wp, intptr_t lp);  // GameInfo::message

// GameInfo::pad: the first with the controller's report alone, the second with what would go to the game
void SeeController(int port, const input::Pad& pad);
void GateReport(int port, input::Pad& pad);

// the settings file, GameInfo::load and GameInfo::save; each takes a toml::table
void LoadSettings(const void* table);
void SaveSettings(void* table);

// hold to skip: the title's thread, from src/game/movie_probe.cpp
void MovieOpened();
void MovieClosed();
bool MovieSkipFrame();  // each frame of a video; true when it should end
float MovieSkipProgress();

}  // namespace options
