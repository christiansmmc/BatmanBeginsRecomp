// The mouse as a way to look around: raw movement from the window, the cursor kept inside it while the game has the mouse,
// and what the camera takes each frame. The camera's side is in src/game/hooks.cpp, the arithmetic in mouse_look_math.h.
// See docs/TECHNICAL.md, "Mouse look".
#pragma once
#include <cstdint>

#include "common/mouse_look_math.h"

namespace mouse {

// the settings file: mouse_look, mouse_sensitivity (degrees of turn per mouse count) and mouse_invert_x / mouse_invert_y
void Configure(bool enabled, float degrees_per_count, bool invert_x, bool invert_y);
void SetInvertX(bool enabled);
void SetInvertY(bool enabled);
bool InvertX();
bool Enabled();
float DegreesPerCount();
bool InvertY();
Tuning CurrentTuning();

// GameInfo::window: asks Windows for the raw mouse
void Init(void* hwnd);
// GameInfo::message, window thread: raw movement, and the focus changes that take or give back the cursor
void OnWindowMessage(void* hwnd, uint32_t message, uintptr_t wp, intptr_t lp);
// GameInfo::ui, each frame: the options screen opening or closing changes who has the mouse
void Sync();

// the game has the mouse: the window is in front, the cursor is hidden and kept inside it, and the buttons count
bool Capturing();

struct Motion {
    bool active;  // the game has the mouse; false resets what the aim kept
    int32_t dx;   // counts to the right since the last call
    int32_t dy;   // counts downwards
};
// the title's thread, once per camera frame. Movement from a stretch in which the camera did not ask (a menu, a cutscene) is
// dropped, so it does not turn the camera when play comes back.
Motion TakeMotion();

}  // namespace mouse
