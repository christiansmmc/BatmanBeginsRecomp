// Everything is laid out on a 1280x720 reference, scaled evenly and centred, and drawn at the window's real resolution.
#include "common/options_screen.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/fps_policy.h"
#include "common/keyboard_pad.h"
#include "common/movie_skip.h"
#include "common/options_model.h"
#include "core/input.h"
#include "core/log.h"

extern const unsigned char kFontMichroma[];
extern const unsigned kFontMichromaSize;
extern const unsigned char kFontBarlowMedium[];
extern const unsigned kFontBarlowMediumSize;
extern const unsigned char kFontBarlowSemiBold[];
extern const unsigned kFontBarlowSemiBoldSize;
extern PlayRateSetting g_play_rate;  // game_info.cpp: experimental_play_fps

namespace options {

namespace {

constexpr uint32_t kGuestWidth = 640, kGuestHeight = 480;
constexpr int kPorts = 4;
constexpr float kRefWidth = 1280.0f, kRefHeight = 720.0f;

constexpr uint16_t kPadUp = 1 << 0, kPadDown = 1 << 1, kPadLeft = 1 << 2, kPadRight = 1 << 3, kPadStart = 1 << 4, kPadBack = 1 << 5;
constexpr int kAnalogA = 0, kAnalogB = 1, kAnalogLeftTrigger = 6, kAnalogRightTrigger = 7;
constexpr uint8_t kAnalogPressed = 128;
constexpr int kStickNav = 16000;

// ---- fonts ----

// the CSS sizes the em; ImGui sizes ascent to descent
constexpr float kMichromaHeightPerEm = 1.421875f, kBarlowHeightPerEm = 1.2f;
constexpr float kTiers[] = {1.0f, 2.0f, 3.0f};  // the reference scales baked, so text stays sharp at any window size

struct Face {
    ImFont* tier[3] = {};
    float height_per_em = 1.0f;
};

Face g_title, g_body, g_small, g_description, g_dot;

ImFont* Pick(const Face& face, float height) {
    for (ImFont* font : face.tier)
        if (font && font->FontSize + 0.5f >= height) return font;
    return face.tier[2];
}

void Bake(Face& face, const unsigned char* data, unsigned size, float em, float height_per_em, const ImWchar* ranges) {
    face.height_per_em = height_per_em;
    for (int i = 0; i < 3; i++) {
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = i == 0 ? 3 : 2;
        config.OversampleV = 1;
        face.tier[i] = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(size),
                                                                  em * height_per_em * kTiers[i], &config, ranges);
    }
}

// ---- colours ----

constexpr ImU32 Rgb(uint32_t hex, float alpha = 1.0f) {
    return ImU32((hex >> 16 & 0xFF) | (hex & 0xFF00) | (hex & 0xFF) << 16 | uint32_t(alpha * 255.0f + 0.5f) << 24);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    const auto channel = [&](int shift) {
        const float x = float(a >> shift & 0xFF), y = float(b >> shift & 0xFF);
        return uint32_t(x + (y - x) * t + 0.5f) << shift;
    };
    return channel(0) | channel(8) | channel(16) | channel(24);
}

struct Stop {
    float at;
    ImU32 color;
};

ImU32 Sample(const Stop* stops, int count, float at) {
    if (at <= stops[0].at) return stops[0].color;
    for (int i = 1; i < count; i++)
        if (at <= stops[i].at) return Mix(stops[i - 1].color, stops[i].color, (at - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
    return stops[count - 1].color;
}

const ImU32 kGold = Rgb(0xFFD067), kBright = Rgb(0xFFFFE6), kTabOff = Rgb(0xD7B56A), kArrow = Rgb(0xA8843C), kDescription = Rgb(0xF6E8C6);
const ImU32 kBlack = Rgb(0x000000);
const Stop kPlate[] = {{0.0f, Rgb(0x3A3A31)}, {0.55f, Rgb(0x191915)}, {1.0f, Rgb(0x0A0A08)}};
const Stop kPlateOff[] = {{0.0f, Rgb(0x2C250F)}, {1.0f, Rgb(0x17130A)}};
const Stop kBandAcross[] = {{0, Rgb(0xA57B0A)},    {214, Rgb(0x9B740A)},  {268, Rgb(0x6D520B)},  {322, Rgb(0x241D0C)}, {372, Rgb(0x100E0A)},
                            {1056, Rgb(0x100E0A)}, {1110, Rgb(0x241D0C)}, {1168, Rgb(0x6D520B)}, {1280, Rgb(0xA57B0A)}};
const Stop kSelectedAcross[] = {{0, Rgb(0xB58A12)},   {230, Rgb(0xB58A12)},  {330, Rgb(0x7A5A0E)},  {520, Rgb(0x4E3A0C)},
                                {900, Rgb(0x4E3A0C)}, {1090, Rgb(0x7A5A0E)}, {1280, Rgb(0xC29614)}};

// ---- drawing on the reference space ----

struct Canvas {
    ImDrawList* list;
    float scale, left, top;  // reference to window
    float ref_left, ref_right;  // the window's edges on the reference

    ImVec2 At(float x, float y) const { return {left + x * scale, top + y * scale}; }
};

Canvas g_canvas;

void Rect(float x0, float y0, float x1, float y1, ImU32 color) { g_canvas.list->AddRectFilled(g_canvas.At(x0, y0), g_canvas.At(x1, y1), color); }

void Line(float x0, float y0, float x1, float y1, ImU32 color, float thickness = 1.0f) {
    g_canvas.list->AddLine(g_canvas.At(x0, y0), g_canvas.At(x1, y1), color, thickness * g_canvas.scale);
}

// a convex polygon with each corner's own colour
void Shaded(const ImVec2* points, const ImU32* colors, int count) {
    ImDrawList* list = g_canvas.list;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    list->PrimReserve((count - 2) * 3, count);
    const ImDrawIdx base = ImDrawIdx(list->_VtxCurrentIdx);
    for (int i = 0; i < count; i++) list->PrimWriteVtx(g_canvas.At(points[i].x, points[i].y), uv, colors[i]);
    for (int i = 2; i < count; i++) {
        list->PrimWriteIdx(base);
        list->PrimWriteIdx(ImDrawIdx(base + i - 1));
        list->PrimWriteIdx(ImDrawIdx(base + i));
    }
}

// a convex polygon filled with a gradient from top to bottom of its own box
void PlateFill(const ImVec2* points, int count, const Stop* stops, int stop_count) {
    float low = points[0].y, high = points[0].y;
    for (int i = 1; i < count; i++) {
        low = std::min(low, points[i].y);
        high = std::max(high, points[i].y);
    }
    for (int s = 1; s < stop_count; s++) {
        const float y0 = low + (high - low) * stops[s - 1].at, y1 = low + (high - low) * stops[s].at;
        // this band of the polygon
        std::vector<ImVec2> band;
        for (int i = 0; i < count; i++) {
            const ImVec2 a = points[i], b = points[(i + 1) % count];
            const auto add_crossing = [&](float y) {
                if ((a.y < y) != (b.y < y)) band.push_back({a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y), y});
            };
            if (a.y >= y0 && a.y <= y1) band.push_back(a);
            if (a.y < b.y) {
                add_crossing(y0);
                add_crossing(y1);
            } else {
                add_crossing(y1);
                add_crossing(y0);
            }
        }
        if (band.size() < 3) continue;
        std::vector<ImU32> colors;
        for (const ImVec2& p : band) colors.push_back(Mix(stops[s - 1].color, stops[s].color, y1 > y0 ? (p.y - y0) / (y1 - y0) : 0.0f));
        Shaded(band.data(), colors.data(), int(band.size()));
    }
}

void Outline(const ImVec2* points, int count, ImU32 color, float thickness) {
    ImVec2 window[8];
    for (int i = 0; i < count; i++) window[i] = g_canvas.At(points[i].x, points[i].y);
    g_canvas.list->AddPolyline(window, count, color, ImDrawFlags_Closed, thickness * g_canvas.scale);
}

// the game's slanted plate: filled, a black stroke, then a thin light one
void Plate(float left, float bottom, float slant, float right, float height, const Stop* stops, int stop_count, float black, ImU32 line, float line_width) {
    const ImVec2 points[4] = {{left, bottom}, {left + slant, bottom - height}, {right, bottom - height}, {right - slant, bottom}};
    PlateFill(points, 4, stops, stop_count);
    Outline(points, 4, kBlack, black);
    Outline(points, 4, line, line_width);
}

float SpacedWidth(ImFont* font, float height, const char* text, float spacing) {
    float width = 0.0f;
    for (const char* p = text; *p;) {
        unsigned c = 0;
        p += ImTextCharFromUtf8(&c, p, nullptr);
        width += font->GetCharAdvance(ImWchar(c)) * height / font->FontSize + spacing;
    }
    return width;
}

void SpacedAt(ImFont* font, float height, ImVec2 at, ImU32 color, const char* text, float spacing) {
    for (const char* p = text; *p;) {
        unsigned c = 0;
        const int length = ImTextCharFromUtf8(&c, p, nullptr);
        g_canvas.list->AddText(font, height, at, color, p, p + length);
        at.x += font->GetCharAdvance(ImWchar(c)) * height / font->FontSize + spacing;
        p += length;
    }
}

float TextWidth(const Face& face, float em, const char* text, float spacing_em = 0.0f) {
    const float height = em * face.height_per_em * g_canvas.scale;
    return SpacedWidth(Pick(face, height), height, text, spacing_em * em * g_canvas.scale) / g_canvas.scale;
}

// x and box as CSS has them: the left edge, and the line box the text is centred in; the shadow is one pixel off
void Text(const Face& face, float em, float x, float box_top, float box_height, ImU32 color, const char* text, float spacing_em = 0.0f) {
    const float height = em * face.height_per_em * g_canvas.scale;
    ImFont* font = Pick(face, height);
    const float spacing = spacing_em * em * g_canvas.scale;
    const float y = box_top + (box_height - em * face.height_per_em) / 2.0f;
    const ImVec2 at = g_canvas.At(x, y);
    SpacedAt(font, height, {at.x + g_canvas.scale, at.y + g_canvas.scale}, kBlack, text, spacing);
    SpacedAt(font, height, at, color, text, spacing);
}

void TextCentred(const Face& face, float em, float centre, float box_top, float box_height, ImU32 color, const char* text, float spacing_em = 0.0f) {
    Text(face, em, centre - TextWidth(face, em, text, spacing_em) / 2.0f, box_top, box_height, color, text, spacing_em);
}

// "3x · 1920 x 1440": Michroma's own middle dot hardly shows
void ValueCentred(float centre, float box_top, float box_height, ImU32 color, const std::string& value) {
    constexpr float kEm = 17.0f, kSpacing = 0.03f, kDotEm = 22.0f;
    const size_t dot = value.find("·");
    if (dot == std::string::npos) {
        TextCentred(g_body, kEm, centre, box_top, box_height, color, value.c_str(), kSpacing);
        return;
    }
    const std::string before = value.substr(0, dot + 0), after = value.substr(dot + std::strlen("·"));
    const float width_before = TextWidth(g_body, kEm, before.c_str(), kSpacing), width_dot = TextWidth(g_dot, kDotEm, "·"),
                width_after = TextWidth(g_body, kEm, after.c_str(), kSpacing);
    float x = centre - (width_before + width_dot + width_after) / 2.0f;
    Text(g_body, kEm, x, box_top, box_height, color, before.c_str(), kSpacing);
    x += width_before;
    Text(g_dot, kDotEm, x, box_top + 2.0f, box_height, color, "·");
    x += width_dot;
    Text(g_body, kEm, x, box_top, box_height, color, after.c_str(), kSpacing);
}

void GradientRoundedRect(float x0, float y0, float x1, float y1, float rounding, ImU32 top, ImU32 bottom) {
    ImDrawList* list = g_canvas.list;
    const int first = list->VtxBuffer.Size;
    list->AddRectFilled(g_canvas.At(x0, y0), g_canvas.At(x1, y1), kBlack, rounding * g_canvas.scale);
    const float low = g_canvas.At(0, y0).y, high = g_canvas.At(0, y1).y;
    for (int i = first; i < list->VtxBuffer.Size; i++) {
        ImDrawVert& v = list->VtxBuffer[i];
        const ImU32 alpha = v.col & IM_COL32_A_MASK;
        const ImU32 color = Mix(top, bottom, std::clamp((v.pos.y - low) / (high - low), 0.0f, 1.0f));
        v.col = (color & ~IM_COL32_A_MASK) | alpha;
    }
}

void KeyCap(float x, float y, float width, float height, float em, float spacing_em, const char* text) {
    const float s = g_canvas.scale;
    g_canvas.list->AddRectFilled(g_canvas.At(x, y + 2.0f), g_canvas.At(x + width, y + height + 2.0f), kBlack, 4.0f * s);
    GradientRoundedRect(x, y, x + width, y + height, 4.0f, Rgb(0x49534E), Rgb(0x20241F));
    g_canvas.list->AddRect(g_canvas.At(x, y), g_canvas.At(x + width, y + height), Rgb(0x8C9E93), 4.0f * s, 0, 1.0f * s);
    TextCentred(g_small, em, x + width / 2.0f, y, height - 3.0f, kBright, text, spacing_em);
}

// the controller's buttons: a dark ring and a ball lit from above
void PadButton(float x, float y, ImU32 light, ImU32 mid, ImU32 dark, const char* letter) {
    constexpr float kSize = 38.0f;
    const float s = g_canvas.scale, radius = kSize / 2.0f;
    const ImVec2 centre = g_canvas.At(x + radius, y + radius);
    g_canvas.list->AddCircleFilled(centre, (radius + 0.5f) * s, Rgb(0x5D6961), 48);
    g_canvas.list->AddCircleFilled(centre, radius * s, Rgb(0x050505), 48);
    const float inner = radius - 2.0f;
    const Stop stops[] = {{0.0f, light}, {0.45f, mid}, {1.0f, dark}};
    constexpr int kRings = 14;
    const float offset_y = -0.12f * kSize;  // the light's centre is at 38% of the height
    for (int ring = 0; ring < kRings; ring++) {
        const float t = 1.0f - float(ring) / float(kRings);
        const ImVec2 at = {centre.x, centre.y + offset_y * s * (1.0f - t)};
        g_canvas.list->AddCircleFilled(at, inner * t * s, Sample(stops, 3, t), 40);
    }
    TextCentred(g_small, 15.0f, x + radius, y, kSize - 4.0f, Rgb(0xFBFBDC), letter);
}

// ---- the screen's state, under app::UiLock ----

enum class Tab { Video, Keyboard };
enum VideoRow { kRowMode, kRowSize, kRowScale, kRowAspect, kRowFilter, kRowVsync, kRowFrameRate, kVideoRows };
enum class Device { Keyboard, Controller };

constexpr float kRowsTop = 148.0f, kRowHeight = 52.0f;
constexpr int kVisibleRows = 7;

struct KeyRow {
    Action action;
    const char* key;    // in the settings file
    const char* label;  // on the screen
};

constexpr KeyRow kKeyRows[] = {
    {Action::MoveForward, "key_move_forward", "MOVE FORWARD"},
    {Action::MoveBack, "key_move_back", "MOVE BACK"},
    {Action::MoveLeft, "key_move_left", "MOVE LEFT"},
    {Action::MoveRight, "key_move_right", "MOVE RIGHT"},
    {Action::Punch, "key_punch", "PUNCH (X)"},
    {Action::Kick, "key_kick", "KICK (Y)"},
    {Action::Jump, "key_jump", "JUMP (A)"},
    {Action::Attack, "key_attack", "ACTION (B)"},
    {Action::LeftTrigger, "key_left_trigger", "LEFT TRIGGER (LT)"},
    {Action::RightTrigger, "key_right_trigger", "RIGHT TRIGGER (RT)"},
    {Action::White, "key_white", "WHITE BUTTON"},
    {Action::Black, "key_black", "BLACK BUTTON"},
    {Action::LookUp, "key_look_up", "CAMERA UP"},
    {Action::LookDown, "key_look_down", "CAMERA DOWN"},
    {Action::LookLeft, "key_look_left", "CAMERA LEFT"},
    {Action::LookRight, "key_look_right", "CAMERA RIGHT"},
    {Action::Start, "key_start", "START"},
    {Action::Back, "key_back", "BACK"},
    {Action::PadUp, "key_pad_up", "D-PAD UP"},
    {Action::PadDown, "key_pad_down", "D-PAD DOWN"},
    {Action::PadLeft, "key_pad_left", "D-PAD LEFT"},
    {Action::PadRight, "key_pad_right", "D-PAD RIGHT"},
    {Action::LeftClick, "key_left_click", "LEFT STICK CLICK"},
    {Action::RightClick, "key_right_click", "RIGHT STICK CLICK"},
};
constexpr int kKeyRowCount = int(std::size(kKeyRows));

struct KeyEvent {
    uint32_t virtual_key;
    bool repeat;
};

struct ScreenState {
    bool open = false;
    Tab tab = Tab::Video;
    int video_row = kRowScale;
    int key_row = 0;
    int key_top = 0;  // the first key row shown
    bool capturing = false;
    Device device = Device::Keyboard;
    std::deque<KeyEvent> key_events;
    Chord open_chord, accept, back, previous_tab, next_tab;
    NavRepeat up, down, left, right;
    uint16_t last_buttons = 0;
    ImVec2 last_mouse = {-1.0f, -1.0f};
    std::chrono::steady_clock::time_point last_frame = std::chrono::steady_clock::now();
};

ScreenState g_screen;
std::atomic<bool> g_open{false};

// ---- the pads, on any thread ----

std::mutex g_pad_lock;
input::Pad g_controller[kPorts];
input::Pad g_final[kPorts];
PadGate g_gates[kPorts];

input::Pad MergedController() {
    input::Pad merged;
    std::lock_guard lock(g_pad_lock);
    for (const input::Pad& pad : g_controller) {
        merged.buttons |= pad.buttons;
        for (int i = 0; i < 8; i++) merged.analog[i] = std::max(merged.analog[i], pad.analog[i]);
        const auto stronger = [](int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
        merged.lx = stronger(merged.lx, pad.lx);
        merged.ly = stronger(merged.ly, pad.ly);
        merged.rx = stronger(merged.rx, pad.rx);
        merged.ry = stronger(merged.ry, pad.ry);
    }
    return merged;
}

void PollPads() {
    for (int port = 0; port < kPorts; port++) (void)input::Poll(port);
}

// ---- hold to skip ----

std::mutex g_skip_lock;
MovieSkip g_skip;
bool g_movie_playing = false;
std::chrono::steady_clock::time_point g_skip_last;
std::atomic<float> g_skip_progress{0.0f};
std::atomic<bool> g_skip_by_controller{true};

bool JumpDownForTheGame() {
    PollPads();
    std::lock_guard lock(g_pad_lock);
    for (const input::Pad& pad : g_final)
        if (pad.analog[kAnalogA] >= kAnalogPressed) return true;
    return false;
}

// ---- changing the settings ----

void SetOutput(app::DisplayMode mode, uint32_t width, uint32_t height) {
    app::Settings& s = app::Get();
    const uint32_t old_height = app::Output().height;
    s.display_mode = mode;
    s.width = width;
    s.height = height;
    s.render_percent = PercentForNewOutput(s.render_percent, old_height, app::Output().height, kGuestHeight);
    app::ApplyDisplay();
}

template <typename T>
int WrapIndex(int index, int delta, const std::vector<T>& list) {
    const int count = int(list.size());
    return ((index + delta) % count + count) % count;
}

std::vector<WindowSize> CurrentSizes() {
    const app::Settings& s = app::Get();
    const app::Mode desktop = app::Desktop();
    return WindowSizes(desktop.width, desktop.height, s.width, s.height, s.display_mode == app::DisplayMode::Windowed);
}

std::vector<ScaleChoice> CurrentScales() {
    return ScaleChoices(app::Get().render_percent, app::Output().height, kGuestWidth, kGuestHeight);
}

constexpr uint32_t kFilters[] = {0, 2, 4, 8, 16};

void ChangeValue(int row, int delta) {
    app::Settings& s = app::Get();
    switch (row) {
        case kRowMode: {
            // in the order on the screen: window, borderless, full screen
            constexpr app::DisplayMode kOrder[] = {app::DisplayMode::Windowed, app::DisplayMode::Borderless, app::DisplayMode::Fullscreen};
            int index = 0;
            while (kOrder[index] != s.display_mode) index++;
            index = ((index + delta) % 3 + 3) % 3;
            if (kOrder[index] == app::DisplayMode::Windowed) SetOutput(kOrder[index], 1280, 720);
            else SetOutput(kOrder[index], 0, 0);
            break;
        }
        case kRowSize: {
            if (s.display_mode == app::DisplayMode::Borderless) return;
            const std::vector<WindowSize> sizes = CurrentSizes();
            if (sizes.empty()) return;
            int index = -1;
            for (int i = 0; i < int(sizes.size()); i++)
                if (sizes[i].width == s.width && sizes[i].height == s.height) index = i;
            index = index < 0 ? (delta > 0 ? 0 : int(sizes.size()) - 1) : WrapIndex(index, delta, sizes);
            SetOutput(s.display_mode, sizes[index].width, sizes[index].height);
            break;
        }
        case kRowScale: {
            const std::vector<ScaleChoice> choices = CurrentScales();
            int index = 0;
            for (int i = 0; i < int(choices.size()); i++)
                if (choices[i].percent == s.render_percent) index = i;
            s.render_percent = choices[WrapIndex(index, delta, choices)].percent;
            break;
        }
        case kRowAspect:
            s.aspect = app::Widescreen() ? app::Aspect::Standard : app::Aspect::Wide;
            break;
        case kRowFilter: {
            int index = 0;
            for (int i = 0; i < int(std::size(kFilters)); i++)
                if (kFilters[i] <= s.aniso) index = i;
            s.aniso = kFilters[((index + delta) % int(std::size(kFilters)) + int(std::size(kFilters))) % int(std::size(kFilters))];
            break;
        }
        case kRowVsync:
            s.vsync = !s.vsync;
            break;
        case kRowFrameRate:
            g_play_rate.Choose(g_play_rate.Chosen() == kExperimentalPlayFps ? 0 : kExperimentalPlayFps);
            break;
        default:
            return;
    }
    app::Save();
}

void ToggleScreen() {
    ScreenState& ui = g_screen;
    ui.open = !ui.open;
    ui.capturing = false;
    ui.last_mouse = ImGui::GetIO().MousePos;  // a cursor resting on a row does not select it
    g_open.store(ui.open);
    app::AskPause(ui.open);
}

void MoveKeyRow(int delta) {
    ScreenState& ui = g_screen;
    ui.key_row = ((ui.key_row + delta) % kKeyRowCount + kKeyRowCount) % kKeyRowCount;
    if (ui.key_row < ui.key_top) ui.key_top = ui.key_row;
    if (ui.key_row >= ui.key_top + kVisibleRows) ui.key_top = ui.key_row - kVisibleRows + 1;
}

void MoveRow(int delta) {
    ScreenState& ui = g_screen;
    if (ui.tab == Tab::Video) ui.video_row = ((ui.video_row + delta) % kVideoRows + kVideoRows) % kVideoRows;
    else MoveKeyRow(delta);
}

void ChangeSelected(int delta) {
    ScreenState& ui = g_screen;
    if (ui.tab == Tab::Video) ChangeValue(ui.video_row, delta);
    else if (delta > 0) ui.capturing = true;
}

void SwitchTab() { g_screen.tab = g_screen.tab == Tab::Video ? Tab::Keyboard : Tab::Video; }

// ---- input ----

void HandleKeyEvent(const KeyEvent& event) {
    ScreenState& ui = g_screen;
    ui.device = Device::Keyboard;
    constexpr uint32_t kEsc = VK_ESCAPE;
    if (ui.capturing) {
        if (event.repeat) return;
        if (event.virtual_key == kEsc) {
            ui.capturing = false;
            return;
        }
        KeyMap map = WindowKeyMap();
        if (RebindKey(map, kKeyRows[ui.key_row].action, event.virtual_key)) {
            SetWindowKeyMap(map);
            ui.capturing = false;
            app::Save();
        }
        return;
    }
    if (event.virtual_key == kEsc) {
        if (!event.repeat) ToggleScreen();
        return;
    }
    if (!ui.open) return;
    switch (event.virtual_key) {
        case VK_UP: MoveRow(-1); break;
        case VK_DOWN: MoveRow(1); break;
        case VK_LEFT: if (ui.tab == Tab::Video) ChangeSelected(-1); break;
        case VK_RIGHT: if (ui.tab == Tab::Video) ChangeSelected(1); break;
        case VK_RETURN:
        case VK_SPACE: if (!event.repeat) ChangeSelected(1); break;
        case VK_TAB: if (!event.repeat) SwitchTab(); break;
        default: break;
    }
}

void HandleController(double seconds) {
    ScreenState& ui = g_screen;
    const input::Pad pad = MergedController();
    const bool chord_started = ui.open_chord.Update((pad.buttons & kPadBack) && (pad.buttons & kPadStart));
    const bool a = pad.analog[kAnalogA] >= kAnalogPressed, b = pad.analog[kAnalogB] >= kAnalogPressed;
    const bool lt = pad.analog[kAnalogLeftTrigger] >= kAnalogPressed, rt = pad.analog[kAnalogRightTrigger] >= kAnalogPressed;
    const bool up = (pad.buttons & kPadUp) || pad.ly > kStickNav, down = (pad.buttons & kPadDown) || pad.ly < -kStickNav;
    const bool left = (pad.buttons & kPadLeft) || pad.lx < -kStickNav, right = (pad.buttons & kPadRight) || pad.lx > kStickNav;
    const bool accept_started = ui.accept.Update(a), back_started = ui.back.Update(b);
    const bool previous_started = ui.previous_tab.Update(lt), next_started = ui.next_tab.Update(rt);
    const bool go_up = ui.up.Update(up, seconds), go_down = ui.down.Update(down, seconds);
    const bool go_left = ui.left.Update(left, seconds), go_right = ui.right.Update(right, seconds);

    if (chord_started || accept_started || back_started || previous_started || next_started || go_up || go_down || go_left || go_right)
        ui.device = Device::Controller;

    if (ui.capturing) {
        if (back_started) ui.capturing = false;
        return;
    }
    if (chord_started) {
        ToggleScreen();
        return;
    }
    if (!ui.open) return;
    if (back_started) {
        ToggleScreen();
        return;
    }
    if (previous_started || next_started) SwitchTab();
    if (go_up) MoveRow(-1);
    if (go_down) MoveRow(1);
    if (ui.tab == Tab::Video) {
        if (go_left) ChangeSelected(-1);
        if (go_right) ChangeSelected(1);
    }
    if (accept_started) ChangeSelected(1);
}

constexpr float kArrowLeft = 760.0f, kArrowRight = 1050.0f, kArrowWidth = 10.0f, kArrowMargin = 12.0f;

bool RowHasArrows(int row) { return !(row == kRowSize && app::Get().display_mode == app::DisplayMode::Borderless); }

// a cursor passing over the window while another program is in front is not the player choosing a row; a test posts its mouse
// to a window that is never in front, so it says so
bool MouseIsForThisWindow() {
    static const bool forced = std::getenv("BB_DEV_MOUSE") != nullptr;
    return forced || GetForegroundWindow() == static_cast<HWND>(app::Window());
}

void HandleMouse() {
    ScreenState& ui = g_screen;
    const ImGuiIO& io = ImGui::GetIO();
    if (!MouseIsForThisWindow()) return;
    const ImVec2 mouse = io.MousePos;
    const float x = (mouse.x - g_canvas.left) / g_canvas.scale, y = (mouse.y - g_canvas.top) / g_canvas.scale;
    const bool moved = mouse.x != ui.last_mouse.x || mouse.y != ui.last_mouse.y;
    ui.last_mouse = mouse;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (ui.capturing) return;

    const auto over_tab = [&](float top) { return x >= 30.0f && x <= 322.0f && y >= top && y <= top + 44.0f; };
    if (clicked && over_tab(kRowsTop) && ui.tab != Tab::Video) SwitchTab();
    if (clicked && over_tab(kRowsTop + kRowHeight) && ui.tab != Tab::Keyboard) SwitchTab();

    if (ui.tab == Tab::Keyboard && io.MouseWheel != 0.0f) {
        ui.key_top = std::clamp(ui.key_top - int(std::lround(io.MouseWheel)), 0, kKeyRowCount - kVisibleRows);
        ui.key_row = std::clamp(ui.key_row, ui.key_top, ui.key_top + kVisibleRows - 1);
    }

    if (x < 330.0f || x > 1090.0f || y < kRowsTop) return;
    const int shown = int((y - kRowsTop) / kRowHeight);
    if (ui.tab == Tab::Video) {
        if (shown >= kVideoRows) return;
        if (moved) ui.video_row = shown;
        if (clicked && RowHasArrows(shown)) {
            ui.video_row = shown;
            if (x >= kArrowLeft - kArrowMargin && x <= kArrowLeft + kArrowWidth + kArrowMargin) ChangeValue(shown, -1);
            if (x >= kArrowRight - kArrowMargin && x <= kArrowRight + kArrowWidth + kArrowMargin) ChangeValue(shown, 1);
        }
    } else {
        if (shown >= kVisibleRows) return;
        const int row = ui.key_top + shown;
        if (row >= kKeyRowCount) return;
        if (moved) ui.key_row = row;
        if (clicked) {
            ui.key_row = row;
            ui.capturing = true;
        }
    }
}

// ---- drawing ----

float BandTop(float x) { return x <= 1010.0f ? 130.0f : x >= 1048.0f ? 92.0f : 130.0f - (x - 1010.0f); }
float BandBottom(float x) { return x <= 536.0f ? 566.0f : x >= 574.0f ? 528.0f : 566.0f - (x - 536.0f); }

void DrawBand() {
    const Canvas& c = g_canvas;
    // across: flat colour between stops, with the band's steps as breaks too
    std::vector<float> breaks = {c.ref_left, 536.0f, 574.0f, 1010.0f, 1048.0f, c.ref_right};
    for (const Stop& stop : kBandAcross)
        if (stop.at > c.ref_left && stop.at < c.ref_right) breaks.push_back(stop.at);
    std::sort(breaks.begin(), breaks.end());
    for (size_t i = 1; i < breaks.size(); i++) {
        const float x0 = breaks[i - 1], x1 = breaks[i];
        if (x1 <= x0) continue;
        const ImU32 c0 = Sample(kBandAcross, int(std::size(kBandAcross)), x0), c1 = Sample(kBandAcross, int(std::size(kBandAcross)), x1);
        const ImVec2 points[4] = {{x0, BandTop(x0)}, {x1, BandTop(x1)}, {x1, BandBottom(x1)}, {x0, BandBottom(x0)}};
        const ImU32 colors[4] = {c0, c1, c1, c0};
        Shaded(points, colors, 4);
    }
    // down: a soft light at the top, a shade at the bottom, and the fine lines
    for (int row = 92; row < 566; row++) {
        const float y = float(row);
        const float left = row < 130 ? 1048.0f - (y - 92.0f) : c.ref_left;
        const float right = row >= 528 ? 574.0f - (y - 528.0f) : c.ref_right;
        const float light = y < 200.0f ? 0.10f * (1.0f - y / 200.0f) : 0.0f;
        const float shade = y > 300.0f ? 0.22f * (y - 300.0f) / 266.0f : 0.0f;
        if (light > 0.0f) Rect(left, y, right, y + 1.0f, Rgb(0xFFDC78, light));
        if (shade > 0.0f) Rect(left, y, right, y + 1.0f, Rgb(0x000000, shade));
        const bool dark_line = row % 5 < 2;
        Rect(left, y, right, y + 1.0f, dark_line ? Rgb(0x000000, 0.22f) : Rgb(0xFFE8AA, 0.035f));
    }
    // the bar code ticks and dotted rules in the gold ends
    const ImU32 tick = Rgb(0xFFDF94, 0.26f), dot = Rgb(0xFFDF94, 0.34f);
    for (float x : {18.0f, 26.0f, 58.0f, 112.0f, 120.0f, 128.0f, 196.0f, 242.0f}) Rect(x - 0.5f, 130.0f, x + 0.5f, 566.0f, tick);
    for (float x : {1152.0f, 1188.0f, 1196.0f, 1224.0f, 1262.0f, 1270.0f}) Rect(x - 0.5f, 92.0f, x + 0.5f, 528.0f, tick);
    const auto dotted = [&](float x0, float x1, float y) {
        for (float x = x0; x < x1; x += 6.0f) Rect(x, y - 0.5f, std::min(x + 2.0f, x1), y + 0.5f, dot);
    };
    for (float y : {316.0f, 412.0f, 508.0f}) dotted(0.0f, 300.0f, y);
    for (float y : {204.0f, 316.0f, 444.0f}) dotted(1130.0f, 1280.0f, y);
}

// the rail along the band's top or bottom edge: nested strokes on one centre line
void Rail(float y_left, float step_x, float y_right) {
    const Canvas& c = g_canvas;
    const ImVec2 points[4] = {c.At(c.ref_left - 10.0f, y_left), c.At(step_x, y_left), c.At(step_x + 38.0f, y_right), c.At(c.ref_right + 10.0f, y_right)};
    const struct {
        ImU32 color;
        float width;
    } kStrokes[] = {{kBlack, 13.0f}, {Rgb(0x46524C), 9.0f}, {Rgb(0x040504), 5.0f}, {Rgb(0xB6C9BB), 1.2f}};
    for (const auto& stroke : kStrokes) c.list->AddPolyline(points, 4, stroke.color, ImDrawFlags_None, stroke.width * c.scale);
}

void DrawArrow(float x, float y, bool to_left, ImU32 color) {
    constexpr float kWidth = 10.0f, kHeight = 16.0f;
    const Canvas& c = g_canvas;
    if (to_left) c.list->AddTriangleFilled(c.At(x + kWidth, y), c.At(x, y + kHeight / 2.0f), c.At(x + kWidth, y + kHeight), color);
    else c.list->AddTriangleFilled(c.At(x, y), c.At(x + kWidth, y + kHeight / 2.0f), c.At(x, y + kHeight), color);
}

void DrawRowBase(int shown, bool selected, bool last) {
    const float top = kRowsTop + float(shown) * kRowHeight;
    const Canvas& c = g_canvas;
    if (selected) {
        std::vector<float> breaks = {c.ref_left, c.ref_right};
        for (const Stop& stop : kSelectedAcross)
            if (stop.at > c.ref_left && stop.at < c.ref_right) breaks.push_back(stop.at);
        std::sort(breaks.begin(), breaks.end());
        for (size_t i = 1; i < breaks.size(); i++) {
            const ImU32 c0 = Sample(kSelectedAcross, int(std::size(kSelectedAcross)), breaks[i - 1]);
            const ImU32 c1 = Sample(kSelectedAcross, int(std::size(kSelectedAcross)), breaks[i]);
            c.list->AddRectFilledMultiColor(c.At(breaks[i - 1], top), c.At(breaks[i], top + kRowHeight), c0, c1, c1, c0);
        }
        Rect(c.ref_left, top, c.ref_right, top + 1.0f, kGold);
        Rect(c.ref_left, top + kRowHeight - 1.0f, c.ref_right, top + kRowHeight, kGold);
        // the slanted marker left of the name
        const ImVec2 marker[4] = {c.At(318.0f + 6.0f, top + 18.0f), c.At(318.0f + 15.0f, top + 18.0f), c.At(318.0f + 9.0f, top + 34.0f), c.At(318.0f, top + 34.0f)};
        c.list->AddConvexPolyFilled(marker, 4, kGold);
    } else if (!last) {
        Rect(344.0f, top + kRowHeight - 1.0f, 1080.0f, top + kRowHeight, Rgb(0x8C9E93, 0.20f));
    }
}

const char* DisplayModeName(app::DisplayMode mode) {
    switch (mode) {
        case app::DisplayMode::Windowed: return "WINDOWED";
        case app::DisplayMode::Borderless: return "BORDERLESS";
        default: return "FULLSCREEN";
    }
}

std::string ValueOf(int row) {
    const app::Settings& s = app::Get();
    switch (row) {
        case kRowMode: return DisplayModeName(s.display_mode);
        case kRowSize: {
            const app::Mode output = app::Output();
            return std::to_string(output.width) + " x " + std::to_string(output.height);
        }
        case kRowScale:
            for (const ScaleChoice& choice : CurrentScales())
                if (choice.percent == s.render_percent) return choice.label;
            return {};
        case kRowAspect: return app::Widescreen() ? "16:9" : "4:3";
        case kRowFilter: return s.aniso ? std::to_string(s.aniso) + "x" : "OFF";
        case kRowVsync: return s.vsync ? "ON" : "OFF";
        case kRowFrameRate: return g_play_rate.Chosen() == kExperimentalPlayFps ? "60 · EXPERIMENTAL" : "30";
        default: return {};
    }
}

const char* DescriptionOf(int row) {
    const app::Settings& s = app::Get();
    switch (row) {
        case kRowMode: return "Windowed, a borderless window the size of the monitor, or fullscreen.";
        case kRowSize:
            if (s.display_mode == app::DisplayMode::Borderless) return "Borderless fills the whole monitor. In Windowed, you can change it.";
            if (s.display_mode == app::DisplayMode::Fullscreen) return "The monitor switches to this resolution while the game has focus.";
            return "The size of the game window.";
        case kRowScale: return "Multiplies the resolution: sharper, but slower. Changes when this screen closes.";
        case kRowAspect: return "Affects the picture and the next saves. Takes effect when this screen closes.";
        case kRowFilter: return "Sharpens textures seen at an angle. Off is the game's own filter.";
        case kRowVsync: return "Prevents screen tearing by syncing with the monitor. Does not change the speed.";
        case kRowFrameRate:
            if (g_play_rate.Chosen() == kExperimentalPlayFps)
                return g_play_rate.WaitsForRestart() ? "Experimental and may be unstable. Takes effect the next time the game starts."
                                                     : "Experimental and may be unstable: some scenes, like the helicopter, run too fast.";
            return g_play_rate.WaitsForRestart() ? "The game's own rate. Takes effect the next time the game starts."
                                                 : "The game's own rate. 60 is smoother, but experimental.";
        default: return "";
    }
}

void DrawVideoRows() {
    const ScreenState& ui = g_screen;
    for (int row = 0; row < kVideoRows; row++) {
        const bool selected = row == ui.video_row;
        const float top = kRowsTop + float(row) * kRowHeight;
        DrawRowBase(row, selected, row == kVideoRows - 1);
        static constexpr const char* kLabels[kVideoRows] = {"DISPLAY MODE", "WINDOW SIZE", "INTERNAL RESOLUTION", "ASPECT RATIO", "TEXTURE FILTER", "VSYNC", "FRAME RATE"};
        Text(g_body, 17.0f, 344.0f, top, kRowHeight, selected ? kBright : kGold, kLabels[row], 0.05f);
        ValueCentred(910.0f, top, kRowHeight, selected ? kBright : kGold, ValueOf(row));
        if (RowHasArrows(row)) {
            const ImU32 arrow = selected ? kGold : kArrow;
            DrawArrow(kArrowLeft, top + 18.0f, true, arrow);
            DrawArrow(kArrowRight, top + 18.0f, false, arrow);
        }
    }
}

void DrawKeyRows() {
    const ScreenState& ui = g_screen;
    const KeyMap map = WindowKeyMap();
    for (int shown = 0; shown < kVisibleRows; shown++) {
        const int row = ui.key_top + shown;
        const bool selected = row == ui.key_row;
        const float top = kRowsTop + float(shown) * kRowHeight;
        DrawRowBase(shown, selected, shown == kVisibleRows - 1);
        Text(g_body, 17.0f, 344.0f, top, kRowHeight, selected ? kBright : kGold, kKeyRows[row].label, 0.05f);
        if (selected && ui.capturing) {
            TextCentred(g_body, 17.0f, 910.0f, top, kRowHeight, kBright, "PRESS A KEY...", 0.03f);
        } else {
            const std::string name = KeyName(map[kKeyRows[row].action]);
            const float width = std::max(44.0f, TextWidth(g_small, 14.0f, name.c_str(), 0.04f) + 38.0f);
            KeyCap(910.0f - width / 2.0f, top + 11.0f, width, 30.0f, 14.0f, 0.04f, name.c_str());
        }
    }
    // how far down the long list this is
    const float bar_top = kRowsTop + 6.0f, bar_height = kRowHeight * kVisibleRows - 12.0f;
    const float thumb = bar_height * kVisibleRows / kKeyRowCount;
    const float start = bar_top + (bar_height - thumb) * float(ui.key_top) / float(kKeyRowCount - kVisibleRows);
    Rect(1096.0f, bar_top, 1098.0f, bar_top + bar_height, Rgb(0x8C9E93, 0.20f));
    Rect(1095.0f, start, 1099.0f, start + thumb, kArrow);
}

void DrawFooter() {
    const ScreenState& ui = g_screen;
    constexpr float kTop = 624.0f, kHeight = 38.0f, kCapTop = 628.0f, kCapWidth = 58.0f, kCapHeight = 30.0f, kEm = 16.0f, kSpacing = 0.08f;
    const auto prompt = [&](float x, const char* text) { Text(g_body, kEm, x, kTop, kHeight, kGold, text, kSpacing); };
    const auto cap = [&](float x, const char* text, float width = kCapWidth) { KeyCap(x, kCapTop, width, kCapHeight, 11.5f, 0.03f, text); };
    if (ui.capturing) {
        cap(1000.0f, "ESC");
        prompt(1072.0f, "CANCEL");
        return;
    }
    if (ui.device == Device::Controller) {
        PadButton(72.0f, kTop, Rgb(0xA9C84A), Rgb(0x6F9226), Rgb(0x39540F), "A");
        prompt(124.0f, "SELECT");
        cap(420.0f, "LT");
        cap(484.0f, "RT");
        prompt(556.0f, "TABS");
        PadButton(800.0f, kTop, Rgb(0xE4635A), Rgb(0xB5302A), Rgb(0x681512), "B");
        prompt(852.0f, "BACK");
    } else {
        cap(72.0f, "ENTER", 72.0f);
        prompt(156.0f, "SELECT");
        cap(420.0f, "TAB");
        prompt(492.0f, "TABS");
    }
    cap(1000.0f, "ESC");
    prompt(1072.0f, "CLOSE");
}

void DrawScreenBody() {
    const ScreenState& ui = g_screen;
    const Canvas& c = g_canvas;
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    // the game darkened behind, solid under the band so the footer reads over bright scenes
    const float dim_end = c.At(0.0f, 566.0f).y, solid = c.At(0.0f, 594.0f).y;
    const ImU32 dim = Rgb(0x040302, 0.70f), full = Rgb(0x040302);
    c.list->AddRectFilled({0.0f, 0.0f}, {size.x, dim_end}, dim);
    c.list->AddRectFilledMultiColor({0.0f, dim_end}, {size.x, solid}, dim, dim, full, full);
    c.list->AddRectFilled({0.0f, solid}, size, full);

    DrawBand();
    Rail(124.0f, 1010.0f, 86.0f);
    Rail(572.0f, 536.0f, 534.0f);

    const int plate_stops = int(std::size(kPlate)), off_stops = int(std::size(kPlateOff));
    const ImU32 plate_line = Rgb(0xA9BCAE);
    Plate(58.0f, 117.0f, 40.0f, 640.0f, 40.0f, kPlate, plate_stops, 5.0f, plate_line, 1.4f);
    Plate(620.0f, 117.0f, 40.0f, 1004.0f, 40.0f, kPlate, plate_stops, 5.0f, plate_line, 1.4f);
    Text(g_title, 25.0f, 112.0f, 79.0f, 38.0f, kGold, "OPTIONS", 0.15f);
    TextCentred(g_small, 13.0f, 812.0f, 79.0f, 38.0f, kGold, "BATMAN BEGINS", 0.24f);

    if (ui.tab == Tab::Video) DrawVideoRows();
    else DrawKeyRows();

    // over the rows: a selected row runs the band's whole width
    const auto tab = [&](float bottom, bool on, const char* name) {
        if (on) Plate(30.0f, bottom, 44.0f, 322.0f, 44.0f, kPlate, plate_stops, 4.0f, kGold, 1.5f);
        else Plate(30.0f, bottom, 44.0f, 286.0f, 44.0f, kPlateOff, off_stops, 3.0f, Rgb(0x6B5A2B), 1.0f);
        Text(g_body, 17.0f, 86.0f, bottom - 44.0f, 44.0f, on ? kBright : kTabOff, name, 0.1f);
    };
    tab(192.0f, ui.tab == Tab::Video, "VIDEO");
    tab(244.0f, ui.tab == Tab::Keyboard, "KEYBOARD");

    Plate(544.0f, 580.0f, 38.0f, 1262.0f, 38.0f, kPlate, plate_stops, 5.0f, plate_line, 1.4f);
    const char* description = ui.tab == Tab::Video ? DescriptionOf(ui.video_row)
                              : ui.capturing       ? "Press the key for this action."
                                                   : "Press to change this command's key. A key already in use swaps places.";
    Text(g_description, 18.0f, 604.0f, 542.0f, 38.0f, kDescription, description, 0.012f);

    DrawFooter();
}

// "HOLD A TO SKIP" and a bar that fills, at the bottom of a video
void DrawSkipNotice(float progress) {
    constexpr float kRight = 1250.0f, kBottom = 690.0f, kHeight = 38.0f, kSlant = 38.0f;
    const std::string key = g_skip_by_controller.load() ? std::string("A") : KeyName(WindowKeyMap()[Action::Jump]);
    const std::string text = "HOLD " + key + " TO SKIP";
    const float text_width = TextWidth(g_small, 13.0f, text.c_str(), 0.08f);
    const float left = kRight - text_width - 2.0f * kSlant - 24.0f;
    Plate(left, kBottom, kSlant, kRight, kHeight, kPlate, int(std::size(kPlate)), 4.0f, Rgb(0xA9BCAE), 1.2f);
    const float text_left = left + kSlant + 8.0f;
    Text(g_small, 13.0f, text_left, kBottom - kHeight + 3.0f, 22.0f, kGold, text.c_str(), 0.08f);
    const float bar_top = kBottom - 11.0f, bar_right = text_left + text_width;
    Rect(text_left, bar_top, bar_right, bar_top + 4.0f, Rgb(0x000000, 0.7f));
    Rect(text_left, bar_top, text_left + (bar_right - text_left) * progress, bar_top + 4.0f, kGold);
}

}  // namespace

void LoadFonts() {
    static constexpr ImWchar kTitleGlyphs[] = {'I', 'I', 'N', 'P', 'S', 'T', 0};  // OPTIONS
    static constexpr ImWchar kTextGlyphs[] = {0x20, 0x7E, 0xC0, 0xFF, 0};
    static constexpr ImWchar kDotGlyph[] = {0xB7, 0xB7, 0};
    Bake(g_title, kFontMichroma, kFontMichromaSize, 25.0f, kMichromaHeightPerEm, kTitleGlyphs);
    Bake(g_body, kFontMichroma, kFontMichromaSize, 17.0f, kMichromaHeightPerEm, kTextGlyphs);
    Bake(g_small, kFontMichroma, kFontMichromaSize, 14.0f, kMichromaHeightPerEm, kTextGlyphs);
    Bake(g_description, kFontBarlowMedium, kFontBarlowMediumSize, 18.0f, kBarlowHeightPerEm, kTextGlyphs);
    Bake(g_dot, kFontBarlowSemiBold, kFontBarlowSemiBoldSize, 22.0f, kBarlowHeightPerEm, kDotGlyph);
}

void DrawScreen() {
    ScreenState& ui = g_screen;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - ui.last_frame).count();
    ui.last_frame = now;

    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0.0f || size.y <= 0.0f) return;
    const float scale = std::min(size.x / kRefWidth, size.y / kRefHeight);
    const float left = (size.x - kRefWidth * scale) / 2.0f, top = (size.y - kRefHeight * scale) / 2.0f;
    g_canvas = {ImGui::GetForegroundDrawList(), scale, left, top, -left / scale, kRefWidth + left / scale};

    PollPads();
    while (!ui.key_events.empty()) {
        const KeyEvent event = ui.key_events.front();
        ui.key_events.pop_front();
        HandleKeyEvent(event);
    }
    HandleController(seconds);
    if (ui.open) {
        HandleMouse();
        DrawScreenBody();
    } else if (const float progress = g_skip_progress.load(); progress > 0.0f) {
        DrawSkipNotice(progress);
    }
}

void OnWindowMessage(uint32_t message, uintptr_t wp, intptr_t lp) {
    if (message != WM_KEYDOWN) return;
    constexpr intptr_t kRepeatBit = intptr_t(1) << 30;
    std::lock_guard lock(app::UiLock());
    g_screen.key_events.push_back({MessageVirtualKey(wp, lp), (lp & kRepeatBit) != 0});
}

void SeeController(int port, const input::Pad& pad) {
    if (port < 0 || port >= kPorts) return;
    std::lock_guard lock(g_pad_lock);
    g_controller[port] = pad;
}

void GateReport(int port, input::Pad& pad) {
    if (port < 0 || port >= kPorts) return;
    std::lock_guard lock(g_pad_lock);
    g_gates[port].Apply(g_open.load(), pad);
    static const bool log_jump = std::getenv("BB_DEV_PAD_LOG") != nullptr;
    if (log_jump && (pad.analog[kAnalogA] >= kAnalogPressed) != (g_final[port].analog[kAnalogA] >= kAnalogPressed))
        LOGR("pad %d A %s for the game", port, pad.analog[kAnalogA] >= kAnalogPressed ? "down" : "up");
    g_final[port] = pad;
}

void LoadSettings(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    KeyMap map = kDefaultKeyMap;
    for (const KeyRow& row : kKeyRows) map[row.action] = t[row.key].value_or(map[row.action]);
    if (ValidKeyMap(map)) SetWindowKeyMap(map);
    else LOGE("settings: the keys in the settings file repeat or use a reserved key, using the defaults");
}

void SaveSettings(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    const KeyMap map = WindowKeyMap();
    for (const KeyRow& row : kKeyRows) t.insert_or_assign(row.key, int64_t(map[row.action]));
}

void MovieOpened() {
    const bool held = JumpDownForTheGame();
    std::lock_guard lock(g_skip_lock);
    g_skip = MovieSkip(held);
    g_movie_playing = true;
    g_skip_last = std::chrono::steady_clock::now();
    g_skip_progress.store(0.0f);
}

void MovieClosed() {
    std::lock_guard lock(g_skip_lock);
    g_movie_playing = false;
    g_skip_progress.store(0.0f);
}

bool MovieSkipFrame() {
    constexpr double kLongestStep = 0.1;  // a hold over the video does not count as holding the button
    const bool held = JumpDownForTheGame();
    bool by_controller = false;
    {
        std::lock_guard lock(g_pad_lock);
        for (const input::Pad& pad : g_controller) by_controller = by_controller || pad.analog[kAnalogA] >= kAnalogPressed;
    }
    std::lock_guard lock(g_skip_lock);
    if (!g_movie_playing) return false;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::min(std::chrono::duration<double>(now - g_skip_last).count(), kLongestStep);
    g_skip_last = now;
    const bool skip = g_skip.Update(held, seconds);
    g_skip_progress.store(skip ? 0.0f : g_skip.Progress());
    if (held) g_skip_by_controller.store(by_controller);
    if (skip) {
        g_movie_playing = false;
        std::lock_guard pads(g_pad_lock);
        for (PadGate& gate : g_gates) gate.BlockHeld();
    }
    return skip;
}

float MovieSkipProgress() { return g_skip_progress.load(); }

}  // namespace options
