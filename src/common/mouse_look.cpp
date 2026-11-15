#include "common/mouse_look.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>

#include "app/app.h"
#include "common/keyboard_pad.h"
#include "core/log.h"

namespace mouse {
namespace {

constexpr UINT kSync = WM_APP + 0x40;  // the host uses WM_APP + 1 and + 2
constexpr float kPi = 3.14159265358979f;
constexpr float kDefaultDegreesPerCount = 0.08f;
// the camera asks every frame in play; a longer silence is a menu, a cutscene or a pause
constexpr std::chrono::milliseconds kSilence{250};

std::atomic<bool> g_enabled{true};
std::atomic<float> g_degrees{kDefaultDegreesPerCount};
std::atomic<bool> g_invert_x{true};
std::atomic<bool> g_invert{true};

std::atomic<bool> g_capturing{false};
std::atomic<bool> g_wanted{false};  // what Sync last saw, so it posts only a change
std::atomic<int32_t> g_dx{0}, g_dy{0};
std::atomic<int64_t> g_last_take{0};  // steady clock ticks of the last TakeMotion, 0 before the first

bool g_cursor_hidden = false;  // window thread only

int64_t Now() { return std::chrono::steady_clock::now().time_since_epoch().count(); }

void ClipToWindow(HWND hwnd) {
    RECT client;
    if (!GetClientRect(hwnd, &client)) return;
    POINT top_left{client.left, client.top}, bottom_right{client.right, client.bottom};
    ClientToScreen(hwnd, &top_left);
    ClientToScreen(hwnd, &bottom_right);
    const RECT screen{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    ClipCursor(&screen);
}

void CentreCursor(HWND hwnd) {
    RECT client;
    if (!GetClientRect(hwnd, &client)) return;
    POINT middle{(client.left + client.right) / 2, (client.top + client.bottom) / 2};
    ClientToScreen(hwnd, &middle);
    SetCursorPos(middle.x, middle.y);
}

// The game has the mouse when it is switched on, the window is in front and no screen of the port's is over the game.
// `inactive` is the window saying it is being left, when the foreground has not changed yet. Window thread.
void Apply(HWND hwnd, bool inactive) {
    const bool want = g_enabled.load() && !inactive && !app::PauseAsked() && GetForegroundWindow() == hwnd && !IsIconic(hwnd);
    if (want == g_capturing.load()) {
        if (want) ClipToWindow(hwnd);  // the window may have moved or changed size
        return;
    }
    if (want) {
        ClipToWindow(hwnd);
        CentreCursor(hwnd);
        if (!g_cursor_hidden) {
            ShowCursor(FALSE);
            g_cursor_hidden = true;
        }
        g_dx.store(0);
        g_dy.store(0);
        g_capturing.store(true);
        LOGI("mouse: the game has the mouse");
    } else {
        g_capturing.store(false);
        ClipCursor(nullptr);
        if (g_cursor_hidden) {
            ShowCursor(TRUE);
            g_cursor_hidden = false;
        }
        // a button held when the mouse goes back would never see its release
        KeyboardWindowMessage(WM_KILLFOCUS, 0, 0);
        LOGI("mouse: the mouse is given back");
    }
}

void Accumulate(LPARAM lp) {
    if (!g_capturing.load(std::memory_order_relaxed)) return;
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (raw.header.dwType != RIM_TYPEMOUSE) return;
    if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) return;  // a tablet, or remote desktop: no device movement to take
    g_dx.fetch_add(raw.data.mouse.lLastX, std::memory_order_relaxed);
    g_dy.fetch_add(raw.data.mouse.lLastY, std::memory_order_relaxed);
}

}  // namespace

void Configure(bool enabled, float degrees_per_count, bool invert_x, bool invert_y) {
    g_enabled.store(enabled);
    // a value that is not a usable speed is the default; a very high one would spin the camera
    const bool usable = degrees_per_count > 0.0f && degrees_per_count <= 5.0f;
    g_degrees.store(usable ? degrees_per_count : kDefaultDegreesPerCount);
    g_invert_x.store(invert_x);
    g_invert.store(invert_y);
    if (!usable) LOGW("settings: mouse_sensitivity %g is not between 0 and 5, using %g", degrees_per_count, kDefaultDegreesPerCount);
}

bool Enabled() { return g_enabled.load(); }
float DegreesPerCount() { return g_degrees.load(); }
void SetInvertX(bool enabled) { g_invert_x.store(enabled); }
void SetInvertY(bool enabled) { g_invert.store(enabled); }
bool InvertX() { return g_invert_x.load(); }
bool InvertY() { return g_invert.load(); }
Tuning CurrentTuning() { return {g_degrees.load() * kPi / 180.0f, g_invert_x.load(), g_invert.load()}; }

void Init(void* hwnd) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01;  // generic desktop
    device.usUsage = 0x02;      // mouse
    device.dwFlags = 0;         // only while this window is in front
    device.hwndTarget = static_cast<HWND>(hwnd);
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) LOGE("mouse: raw input was refused (error %lu)", GetLastError());
}

void OnWindowMessage(void* window, uint32_t message, uintptr_t wp, intptr_t lp) {
    HWND hwnd = static_cast<HWND>(window);
    switch (message) {
        case WM_INPUT:
            Accumulate(static_cast<LPARAM>(lp));
            break;
        case WM_ACTIVATE:
            Apply(hwnd, LOWORD(wp) == WA_INACTIVE);
            break;
        case WM_KILLFOCUS:
            Apply(hwnd, true);
            break;
        case WM_SETFOCUS:
        case kSync:
            Apply(hwnd, false);
            break;
        case WM_SIZE:
        case WM_MOVE:
            if (g_capturing.load()) ClipToWindow(hwnd);
            break;
        default:
            break;
    }
}

void Sync() {
    HWND hwnd = static_cast<HWND>(app::Window());
    if (!hwnd) return;
    const bool want = g_enabled.load() && !app::PauseAsked();
    if (g_wanted.exchange(want) != want) PostMessageW(hwnd, kSync, 0, 0);
}

bool Capturing() { return g_capturing.load(); }

Motion TakeMotion() {
    const int64_t now = Now();
    const int64_t before = g_last_take.exchange(now);
    const int32_t dx = g_dx.exchange(0), dy = g_dy.exchange(0);
    if (!g_capturing.load()) return {false, 0, 0};
    const int64_t silence = std::chrono::duration_cast<std::chrono::steady_clock::duration>(kSilence).count();
    if (before != 0 && now - before > silence) return {true, 0, 0};
    return {true, dx, dy};
}

}  // namespace mouse
