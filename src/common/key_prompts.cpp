#include "common/key_prompts.h"

#include <atomic>
#include <cstdlib>
#include <vector>

#include "common/keyboard_pad.h"
#include "common/prompt_art.h"
#include "gpu/texturedumper.h"

namespace prompts {
namespace {

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_keyboard{true};  // the keyboard was used last; the game starts on a PC unless a controller is there
std::atomic<bool> g_used{false};     // the player has pressed something on either

bool Showing() { return g_enabled.load(std::memory_order_relaxed) && g_keyboard.load(std::memory_order_relaxed); }

// the textures that hold the game's buttons are 128x128 and 64x64; no other size is hashed for them
bool WantsSize(uint32_t width, uint32_t height) {
    for (const Sheet& s : kSheets)
        if (s.w == width && s.h == height) return true;
    return false;
}

bool Paints(uint64_t key, uint32_t width, uint32_t height) {
    const Sheet* sheet = FindSheet(key);
    return sheet && Showing() && width == sheet->w && height == sheet->h;
}

bool Paint(uint64_t key, uint32_t& width, uint32_t& height, std::vector<uint8_t>& bgra) {
    if (!Paints(key, width, height)) return false;
    const Sheet* sheet = FindSheet(key);
    Bitmap bm;
    bm.w = width;
    bm.h = height;
    bm.px = std::move(bgra);
    Bitmap out = PaintSheet(*sheet, WindowKeyMap(), bm);
    width = out.w;
    height = out.h;
    bgra = std::move(out.px);
    return true;
}

// changes with the keys, the setting and the device, which is all a picture depends on: 1 while the console's pictures show,
// whatever the keys, otherwise a hash of the key map. The renderer keeps the pictures of the last one, so going back to the
// keyboard with the same keys paints nothing.
uint32_t Revision() {
    if (!Showing()) return 1u;
    uint32_t h = 2166136261u;
    const KeyMap map = WindowKeyMap();
    for (uint32_t i = 0; i < kActionCount; i++) h = (h ^ map.virtual_key[i]) * 16777619u;
    return h > 1u ? h : 2u;
}

}  // namespace

void Configure(bool enabled) { g_enabled.store(enabled); }
bool Enabled() { return g_enabled.load(); }

void Init() { gpu::texturedumper::SetOverlay({WantsSize, Paints, Paint, Revision}); }

void NoteKeyboard() {
    g_keyboard.store(true, std::memory_order_relaxed);
    g_used.store(true, std::memory_order_relaxed);
}
void NoteController() {
    g_keyboard.store(false, std::memory_order_relaxed);
    g_used.store(true, std::memory_order_relaxed);
}
void NoteControllerConnected() {
    if (!g_used.load(std::memory_order_relaxed)) g_keyboard.store(false, std::memory_order_relaxed);
}

bool ControllerInUse(uint16_t buttons, const uint8_t analog[8], int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    constexpr uint8_t kAnalogDown = 100;
    constexpr int kStickMoved = 16000;  // of 32767: well past a stick that does not rest at the middle
    if (buttons) return true;
    for (int i = 0; i < 8; i++)
        if (analog[i] >= kAnalogDown) return true;
    return std::abs(int(lx)) > kStickMoved || std::abs(int(ly)) > kStickMoved || std::abs(int(rx)) > kStickMoved ||
           std::abs(int(ry)) > kStickMoved;
}

}  // namespace prompts
