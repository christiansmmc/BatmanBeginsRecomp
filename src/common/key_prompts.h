// The game's button pictures as the player's keys. The renderer asks for them through gpu::texturedumper's overlay; see
// docs/TECHNICAL.md, "Key prompts" and src/common/prompt_art.h.
#pragma once
#include <cstdint>

namespace prompts {

// settings.toml: pc_prompts. On, the pictures of A, B, X, Y, the triggers, the black and white buttons, BACK and START show the
// key that does it while the keyboard or the mouse was the last thing the player used, and the console's again after the
// controller.
void Configure(bool enabled);
bool Enabled();

// GameInfo::init: puts the overlay in the renderer
void Init();

// which the player used last; any thread
void NoteKeyboard();
void NoteController();
// a controller is plugged in: until the player presses anything, the console's pictures show
void NoteControllerConnected();

// the controller's report as the game polls it, before the keyboard is added: true when it holds something the player did
bool ControllerInUse(uint16_t buttons, const uint8_t analog[8], int16_t lx, int16_t ly, int16_t rx, int16_t ry);

}  // namespace prompts
