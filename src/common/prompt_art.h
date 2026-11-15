// Keyboard prompts drawn over the console's button icons. The game shows a button as a picture: a glyph of its font, or a
// square of a HUD texture. Here are the places of those pictures in the three textures that hold them (found by dumping the
// game's textures), and the drawing that puts a key cap with the player's key in each place. It works on plain pixels and has
// no Windows in it; src/common/key_prompts.cpp is what hooks it to the renderer. See docs/TECHNICAL.md, "Key prompts".
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/keyboard_pad.h"

namespace prompts {

struct Bitmap {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // B G R A, rows tightly packed
};

// inclusive pixel box
struct Rect {
    int x0, y0, x1, y1;
};

// a button's picture: where it is in the console's texture (pixels of the texture as the game has it) and what it stands for
struct Cell {
    Rect box;
    Action action;
};

// A texture the game's buttons are in, named as the texture dump names it: a hash of the bytes, size and format. `scale` is how
// much bigger the texture is made, so the key caps have room for their letters; the game samples by fractions, so a bigger
// texture is drawn the same.
struct Sheet {
    uint64_t key;
    uint32_t w, h;
    uint32_t scale;
    const Cell* cells;
    size_t count;
};

// 128x128 DXT3, the font's: the buttons in the text ("Press (A)"). The rows are the face buttons, the black and white buttons,
// BACK and the triggers, and START. The directional pad, the stick icons, and the G D I S buttons are left as the console's.
inline constexpr Cell kFontCells[] = {
    {{1, 3, 20, 21}, Action::Jump},       {{23, 3, 42, 21}, Action::Attack},
    {{45, 3, 64, 21}, Action::Punch},     {{67, 3, 86, 21}, Action::Kick},
    {{88, 3, 101, 17}, Action::White},    {{104, 4, 117, 18}, Action::Black},
    {{32, 25, 46, 39}, Action::Back},     {{54, 24, 74, 45}, Action::LeftTrigger},
    {{77, 24, 98, 46}, Action::RightTrigger},
    {{49, 49, 62, 70}, Action::LeftTrigger}, {{67, 49, 80, 70}, Action::RightTrigger},
    {{95, 97, 109, 111}, Action::Start},
};

// the two 64x64 textures with A and B above X and Y (the HUD's)
inline constexpr Cell kHudDxtCells[] = {
    {{1, 3, 20, 21}, Action::Jump},   {{23, 3, 42, 21}, Action::Attack},
    {{1, 27, 20, 45}, Action::Punch}, {{23, 27, 42, 45}, Action::Kick},
};
inline constexpr Cell kHudBgraCells[] = {
    {{1, 1, 20, 23}, Action::Jump},   {{23, 1, 42, 23}, Action::Attack},
    {{1, 25, 20, 47}, Action::Punch}, {{23, 25, 42, 47}, Action::Kick},
};

inline constexpr Sheet kSheets[] = {
    {0xe17fcca2edd00edbull, 128, 128, 4, kFontCells, std::size(kFontCells)},
    {0x3020a8ad19ca2b71ull, 64, 64, 4, kHudDxtCells, std::size(kHudDxtCells)},
    {0x90039937beb703d4ull, 64, 64, 4, kHudBgraCells, std::size(kHudBgraCells)},
};

inline const Sheet* FindSheet(uint64_t key) {
    for (const Sheet& s : kSheets)
        if (s.key == key) return &s;
    return nullptr;
}

// ---- the label of a key: at most three characters ----

inline std::string KeyLabel(uint32_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    if (vk >= 0x60 && vk <= 0x69) return "N" + std::to_string(vk - 0x60);
    switch (vk) {
        case 0x01: return "LMB";
        case 0x02: return "RMB";
        case 0x04: return "MMB";
        case 0x05: return "M4";
        case 0x06: return "M5";
        case 0x08: return "BS";
        case 0x09: return "TAB";
        case 0x0D: return "ENT";
        case 0x10: return "SH";
        case 0x11: return "CT";
        case 0x12: return "AL";
        case 0x14: return "CAP";
        case 0x1B: return "ESC";
        case 0x20: return "SPC";
        case 0x21: return "PGU";
        case 0x22: return "PGD";
        case 0x23: return "END";
        case 0x24: return "HOM";
        case 0x25: return "<";
        case 0x26: return "^";
        case 0x27: return ">";
        case 0x28: return "v";
        case 0x2D: return "INS";
        case 0x2E: return "DEL";
        case 0x6A: return "N*";
        case 0x6B: return "N+";
        case 0x6D: return "N-";
        case 0x6E: return "N.";
        case 0x6F: return "N/";
        case 0xA0: return "LSH";
        case 0xA1: return "RSH";
        case 0xA2: return "LCT";
        case 0xA3: return "RCT";
        case 0xA4: return "LAL";
        case 0xA5: return "RAL";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: return "?";
    }
}

// ---- a 5x7 font ----

struct Glyph {
    char c;
    const char* rows[7];
};

inline constexpr Glyph kFont[] = {
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
    {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
    {'J', {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "11011", "10001"}},
    {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {'5', {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
    {'6', {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}},
    {'^', {"00100", "01110", "10101", "00100", "00100", "00100", "00100"}},
    {'v', {"00100", "00100", "00100", "00100", "10101", "01110", "00100"}},
    {'<', {"00000", "00100", "01000", "11111", "01000", "00100", "00000"}},
    {'>', {"00000", "00100", "00010", "11111", "00010", "00100", "00000"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
    {'=', {"00000", "11111", "00000", "11111", "00000", "00000", "00000"}},
    {'*', {"00000", "10101", "01110", "11111", "01110", "10101", "00000"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "00110", "00110"}},
    {',', {"00000", "00000", "00000", "00000", "00110", "00100", "01000"}},
    {';', {"00000", "00110", "00110", "00000", "00110", "00100", "01000"}},
    {'/', {"00001", "00010", "00010", "00100", "01000", "01000", "10000"}},
    {'\\', {"10000", "01000", "01000", "00100", "00010", "00010", "00001"}},
    {'[', {"01110", "01000", "01000", "01000", "01000", "01000", "01110"}},
    {']', {"01110", "00010", "00010", "00010", "00010", "00010", "01110"}},
    {'\'', {"00100", "00100", "01000", "00000", "00000", "00000", "00000"}},
    {'`', {"01000", "00100", "00010", "00000", "00000", "00000", "00000"}},
    {'?', {"01110", "10001", "00001", "00110", "00100", "00000", "00100"}},
};

inline const Glyph& GlyphOf(char c) {
    for (const Glyph& g : kFont)
        if (g.c == c) return g;
    return kFont[std::size(kFont) - 1];  // '?'
}

// ---- drawing ----

inline Bitmap ScaleUp(const Bitmap& src, uint32_t factor) {
    Bitmap out;
    out.w = src.w * factor;
    out.h = src.h * factor;
    out.px.resize(size_t(out.w) * out.h * 4);
    for (uint32_t y = 0; y < out.h; y++) {
        const float fy = (float(y) + 0.5f) / float(factor) - 0.5f;
        const int y0 = std::clamp(int(std::floor(fy)), 0, int(src.h) - 1), y1 = std::min(y0 + 1, int(src.h) - 1);
        const float ty = std::clamp(fy - float(y0), 0.0f, 1.0f);
        for (uint32_t x = 0; x < out.w; x++) {
            const float fx = (float(x) + 0.5f) / float(factor) - 0.5f;
            const int x0 = std::clamp(int(std::floor(fx)), 0, int(src.w) - 1), x1 = std::min(x0 + 1, int(src.w) - 1);
            const float tx = std::clamp(fx - float(x0), 0.0f, 1.0f);
            for (int c = 0; c < 4; c++) {
                const auto at = [&](int xx, int yy) { return float(src.px[(size_t(yy) * src.w + size_t(xx)) * 4 + size_t(c)]); };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                out.px[(size_t(y) * out.w + x) * 4 + size_t(c)] = uint8_t(std::lround(top + (bottom - top) * ty));
            }
        }
    }
    return out;
}

inline void Over(Bitmap& bm, int x, int y, uint8_t r, uint8_t g, uint8_t b, float cover) {
    if (x < 0 || y < 0 || x >= int(bm.w) || y >= int(bm.h) || cover <= 0.0f) return;
    uint8_t* p = &bm.px[(size_t(y) * bm.w + size_t(x)) * 4];
    const float da = float(p[3]) / 255.0f;
    const float oa = cover + da * (1.0f - cover);
    const auto mix = [&](uint8_t dst, uint8_t src) {
        return uint8_t(std::lround(oa > 0.0f ? (float(src) * cover + float(dst) * da * (1.0f - cover)) / oa : 0.0f));
    };
    p[0] = mix(p[0], b);
    p[1] = mix(p[1], g);
    p[2] = mix(p[2], r);
    p[3] = uint8_t(std::lround(oa * 255.0f));
}

// Replaces what is inside `box` (grown by `margin`) with a key cap and the label on it. The picture it covers is cleared first.
inline void DrawKeycap(Bitmap& bm, Rect box, int margin, const std::string& label) {
    for (int y = box.y0 - margin; y <= box.y1 + margin; y++) {
        for (int x = box.x0 - margin; x <= box.x1 + margin; x++) {
            if (x < 0 || y < 0 || x >= int(bm.w) || y >= int(bm.h)) continue;
            uint8_t* p = &bm.px[(size_t(y) * bm.w + size_t(x)) * 4];
            p[0] = p[1] = p[2] = p[3] = 0;
        }
    }
    const float w = float(box.x1 - box.x0 + 1), h = float(box.y1 - box.y0 + 1);
    const float radius = std::min(w, h) * 0.24f;
    const float border = std::max(1.0f, std::min(w, h) * 0.07f);
    for (int y = box.y0; y <= box.y1; y++) {
        for (int x = box.x0; x <= box.x1; x++) {
            // distance outside the rounded box, negative inside
            const float px = float(x - box.x0) + 0.5f - w * 0.5f, py = float(y - box.y0) + 0.5f - h * 0.5f;
            const float qx = std::fabs(px) - (w * 0.5f - radius), qy = std::fabs(py) - (h * 0.5f - radius);
            const float sd = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
            const float cover = std::clamp(0.5f - sd, 0.0f, 1.0f);
            if (cover <= 0.0f) continue;
            const float down = (float(y - box.y0) + 0.5f) / h;  // 0 at the top
            float r = 96.0f - 44.0f * down, g = 100.0f - 45.0f * down, b = 108.0f - 46.0f * down;
            if (float(y - box.y0) < border * 2.0f && sd < -border) {  // light on the top edge
                r += 26.0f;
                g += 26.0f;
                b += 26.0f;
            }
            if (sd > -border) r = g = b = 22.0f;  // the outline
            Over(bm, x, y, uint8_t(std::clamp(r, 0.0f, 255.0f)), uint8_t(std::clamp(g, 0.0f, 255.0f)),
                 uint8_t(std::clamp(b, 0.0f, 255.0f)), cover);
        }
    }

    const int n = int(label.size());
    if (n == 0) return;
    const int scale = std::max(1, std::min(int(w * 0.74f) / (n * 6 - 1), int(h * 0.58f) / 7));
    const int text_w = (n * 6 - 1) * scale, text_h = 7 * scale;
    const int left = box.x0 + (int(w) - text_w) / 2, top = box.y0 + (int(h) - text_h) / 2 - int(h * 0.02f);
    const int shadow = std::max(1, scale / 3);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            const Glyph& glyph = GlyphOf(label[size_t(i)]);
            for (int row = 0; row < 7; row++) {
                for (int col = 0; col < 5; col++) {
                    if (glyph.rows[row][col] != '1') continue;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            const int x = left + (i * 6 + col) * scale + dx, y = top + row * scale + dy;
                            if (pass == 0) Over(bm, x + shadow, y + shadow, 15, 15, 18, 0.8f);
                            else Over(bm, x, y, 245, 245, 245, 1.0f);
                        }
                    }
                }
            }
        }
    }
}

// The texture of `sheet` (as the game has it, `bm` w x h) made bigger, with a key cap on each button for the keys in `map`.
inline Bitmap PaintSheet(const Sheet& sheet, const KeyMap& map, const Bitmap& bm) {
    Bitmap out = ScaleUp(bm, sheet.scale);
    const int s = int(sheet.scale);
    for (size_t i = 0; i < sheet.count; i++) {
        const Cell& c = sheet.cells[i];
        const Rect scaled{c.box.x0 * s, c.box.y0 * s, (c.box.x1 + 1) * s - 1, (c.box.y1 + 1) * s - 1};
        DrawKeycap(out, scaled, s, KeyLabel(map[c.action]));
    }
    return out;
}

}  // namespace prompts
