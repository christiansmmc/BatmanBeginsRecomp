// The rules of the options screen, without ImGui or global state: options_screen.cpp draws them.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "common/keyboard_pad.h"

namespace input {
struct Pad;
}

namespace options {

// render_percent that gives at least this whole scale of the game's picture, rounded up
uint32_t RenderPercentFor(uint32_t scale, uint32_t output_height, uint32_t guest_height);
// "4x · 2560 x 1920"
std::string ScaleLabel(uint32_t scale, uint32_t guest_width, uint32_t guest_height);

struct ScaleChoice {
    uint32_t percent;
    std::string label;
};
// what the internal resolution row offers, lowest first: the whole scales, the window's own height when that is no whole
// scale, and the current value when it is neither
std::vector<ScaleChoice> ScaleChoices(uint32_t current_percent, uint32_t output_height, uint32_t guest_width,
                                      uint32_t guest_height);
// a whole scale stays that scale at another output height; the window's height and a value set by hand stay as they are
uint32_t PercentForNewOutput(uint32_t percent, uint32_t old_output_height, uint32_t new_output_height, uint32_t guest_height);

struct WindowSize {
    uint32_t width, height;
};
// the sizes that fit the monitor, smallest first, with the current one when it is not among them; a window needs room for
// its title bar, so in one the monitor's own size is left out
std::vector<WindowSize> WindowSizes(uint32_t desktop_width, uint32_t desktop_height, uint32_t current_width,
                                    uint32_t current_height, bool windowed);

// gives the action this key; if another action had it, that one takes this action's old key. False, and nothing changes,
// for a key that is not the game's to have.
bool RebindKey(KeyMap& map, Action action, uint32_t virtual_key);
// each action has a key of its own, none of them reserved
bool ValidKeyMap(const KeyMap& map);
// as the key cap shows it
std::string KeyName(uint32_t virtual_key);

// Stands between the controller and the game: a neutral pad while the screen is open, and a button down when the screen
// closes, or when a video is skipped, reaches the game only after it is let go. Back + Start belongs to the screen, so
// Start stays away from the game while Back is down.
class PadGate {
public:
    void Apply(bool screen_open, input::Pad& pad);
    // what is down at the next report waits for its release
    void BlockHeld() { block_next_ = true; }

private:
    uint16_t blocked_buttons_ = 0;
    uint8_t blocked_analog_ = 0;  // a bit for each of Pad::analog
    uint8_t blocked_sticks_ = 0;  // lx ly rx ry
    bool block_next_ = false;
};

// true once each time something starts being held
class Chord {
public:
    bool Update(bool held) {
        const bool started = held && !held_;
        held_ = held;
        return started;
    }

private:
    bool held_ = false;
};

// a direction held on the controller: once at once, then again after a delay and at a steady pace
class NavRepeat {
public:
    bool Update(bool held, double seconds);

private:
    bool held_ = false;
    double wait_ = 0.0;
};

}  // namespace options
