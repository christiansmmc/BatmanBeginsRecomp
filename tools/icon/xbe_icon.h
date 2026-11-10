// The program's icon from the game's own title image, the one the console's dashboard shows: the $$XTIMAGE section of the
// XBE. Used at build time by tools/icon/main.cpp, so the icon comes from the player's disc and never sits in the repository.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace xbe_icon {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    uint8_t* At(int x, int y) { return rgba.data() + (size_t(y) * w + x) * 4; }
    const uint8_t* At(int x, int y) const { return rgba.data() + (size_t(y) * w + x) * 4; }
};

namespace detail {

inline uint32_t Read32(std::span<const uint8_t> b, uint64_t at) {
    uint32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

inline void Write16(std::vector<uint8_t>& b, uint16_t v) { b.insert(b.end(), {uint8_t(v), uint8_t(v >> 8)}); }
inline void Write32(std::vector<uint8_t>& b, uint32_t v) {
    b.insert(b.end(), {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
}

inline int Lowest(const uint8_t* p) { return std::min({p[0], p[1], p[2]}); }

inline uint8_t Byte(double v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); }

}  // namespace detail

// a section's raw bytes by its name, when the headers and the bytes are all inside the file
inline std::optional<std::span<const uint8_t>> Section(std::span<const uint8_t> xbe, std::string_view name) {
    using detail::Read32;
    if (xbe.size() < 0x124 || std::memcmp(xbe.data(), "XBEH", 4) != 0) return std::nullopt;
    const uint32_t base = Read32(xbe, 0x104), count = Read32(xbe, 0x11C), headers = Read32(xbe, 0x120);
    if (headers < base) return std::nullopt;
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t h = uint64_t(headers - base) + uint64_t(i) * 56;
        if (h + 24 > xbe.size()) return std::nullopt;
        const uint32_t raw = Read32(xbe, h + 12), size = Read32(xbe, h + 16), name_va = Read32(xbe, h + 20);
        if (name_va < base || name_va - base >= xbe.size()) continue;
        const uint64_t n = name_va - base;
        const auto* text = reinterpret_cast<const char*>(xbe.data() + n);
        const size_t room = xbe.size() - n, length = strnlen(text, room);
        if (length == room || std::string_view(text, length) != name) continue;
        if (uint64_t(raw) + size > xbe.size()) return std::nullopt;
        return xbe.subspan(raw, size);
    }
    return std::nullopt;
}

// an XPR0 holding one DXT1 texture, which is what a title image is; anything else is refused
inline std::optional<Image> DecodeXpr(std::span<const uint8_t> xpr) {
    using detail::Read32;
    if (xpr.size() < 32 || std::memcmp(xpr.data(), "XPR0", 4) != 0) return std::nullopt;
    const uint32_t header = Read32(xpr, 8), format = Read32(xpr, 24);
    const uint32_t log_w = (format >> 20) & 15, log_h = (format >> 24) & 15;
    if (((format >> 8) & 0xFF) != 0x0C || log_w < 2 || log_h < 2 || log_w > 12 || log_h > 12) return std::nullopt;
    Image im{1 << log_w, 1 << log_h, {}};
    const uint64_t need = uint64_t(im.w) * im.h / 2;
    if (header > xpr.size() || xpr.size() - header < need) return std::nullopt;
    im.rgba.assign(size_t(im.w) * im.h * 4, 0);
    const uint8_t* block = xpr.data() + header;
    for (int by = 0; by < im.h; by += 4) {
        for (int bx = 0; bx < im.w; bx += 4, block += 8) {
            uint16_t c[2];
            uint32_t bits;
            std::memcpy(c, block, 4);
            std::memcpy(&bits, block + 4, 4);
            int pal[4][4];
            for (int k = 0; k < 2; k++) {
                pal[k][0] = ((c[k] >> 11) & 31) * 255 / 31;
                pal[k][1] = ((c[k] >> 5) & 63) * 255 / 63;
                pal[k][2] = (c[k] & 31) * 255 / 31;
                pal[k][3] = 255;
            }
            for (int ch = 0; ch < 3; ch++) {
                if (c[0] > c[1]) {
                    pal[2][ch] = (2 * pal[0][ch] + pal[1][ch]) / 3;
                    pal[3][ch] = (pal[0][ch] + 2 * pal[1][ch]) / 3;
                } else {
                    pal[2][ch] = (pal[0][ch] + pal[1][ch]) / 2;
                    pal[3][ch] = 0;
                }
            }
            pal[2][3] = 255;
            pal[3][3] = c[0] > c[1] ? 255 : 0;
            for (int i = 0; i < 16; i++) {
                const int* p = pal[(bits >> (2 * i)) & 3];
                uint8_t* out = im.At(bx + i % 4, by + i / 4);
                for (int ch = 0; ch < 4; ch++) out[ch] = uint8_t(p[ch]);
            }
        }
    }
    return im;
}

// The title image is the logo on white. The white that reaches the edges goes; the picture's outermost pixels were blended
// with that white, so they get the alpha and colour that blend over white gives back. White enclosed by the picture stays.
inline void ClearWhiteBackground(Image& im) {
    constexpr int kWhite = 225;  // all three channels at or above: the white around the logo
    constexpr int kSolid = 100;  // on the rim, a pixel this dark or darker is all logo
    std::vector<uint8_t> outside(size_t(im.w) * im.h, 0);
    std::vector<int> todo;
    auto reach = [&](int x, int y) {
        const size_t i = size_t(y) * im.w + x;
        const uint8_t* p = im.At(x, y);
        if (!outside[i] && (p[3] == 0 || detail::Lowest(p) >= kWhite)) {
            outside[i] = 1;
            todo.push_back(int(i));
        }
    };
    for (int x = 0; x < im.w; x++) reach(x, 0), reach(x, im.h - 1);
    for (int y = 0; y < im.h; y++) reach(0, y), reach(im.w - 1, y);
    while (!todo.empty()) {
        const int i = todo.back(), x = i % im.w, y = i / im.w;
        todo.pop_back();
        if (x > 0) reach(x - 1, y);
        if (x + 1 < im.w) reach(x + 1, y);
        if (y > 0) reach(x, y - 1);
        if (y + 1 < im.h) reach(x, y + 1);
    }
    auto touches = [&](int x, int y) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < im.w && ny < im.h && outside[size_t(ny) * im.w + nx]) return true;
            }
        return false;
    };
    std::vector<uint8_t> out = im.rgba;
    for (int y = 0; y < im.h; y++) {
        for (int x = 0; x < im.w; x++) {
            uint8_t* o = out.data() + (size_t(y) * im.w + x) * 4;
            if (outside[size_t(y) * im.w + x]) {
                std::memset(o, 0, 4);
                continue;
            }
            const uint8_t* p = im.At(x, y);
            const int lowest = detail::Lowest(p);
            if (lowest <= kSolid || !touches(x, y)) continue;
            const double a = double(255 - lowest) / double(255 - kSolid);
            for (int ch = 0; ch < 3; ch++) o[ch] = detail::Byte((p[ch] - 255.0 * (1.0 - a)) / a);
            o[3] = detail::Byte(a * p[3]);
        }
    }
    im.rgba.swap(out);
}

// The opaque part, centred in a square and as large as fits in the share of the side given: by default a 32nd of the side
// stays free all round. Smaller than the source it is the average of the area each pixel covers; larger, a bilinear sample.
// Both blend with alpha weighted.
inline Image FitSquare(const Image& src, int size, double fill = 15.0 / 16.0) {
    Image out{size, size, std::vector<uint8_t>(size_t(size) * size * 4, 0)};
    int x0 = src.w, y0 = src.h, x1 = 0, y1 = 0;
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++)
            if (src.At(x, y)[3]) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
    if (x1 <= x0) return out;
    const double bw = x1 - x0, bh = y1 - y0, inner = size * fill;
    const double scale = std::min(inner / bw, inner / bh), ox = (size - bw * scale) / 2, oy = (size - bh * scale) / 2;
    auto pixel = [&](int x, int y, double* premultiplied) {  // outside the opaque part counts as clear
        if (x < x0 || y < y0 || x >= x1 || y >= y1) return 0.0;
        const uint8_t* p = src.At(x, y);
        const double a = p[3] / 255.0;
        for (int ch = 0; ch < 3; ch++) premultiplied[ch] = p[ch] * a;
        return a;
    };
    for (int Y = 0; Y < size; Y++) {
        for (int X = 0; X < size; X++) {
            double sum[3] = {0, 0, 0}, alpha = 0;
            if (scale <= 1.0) {
                const double u0 = (X - ox) / scale + x0, u1 = (X + 1 - ox) / scale + x0;
                const double v0 = (Y - oy) / scale + y0, v1 = (Y + 1 - oy) / scale + y0;
                for (int y = std::max(y0, int(std::floor(v0))); y < std::min(y1, int(std::ceil(v1))); y++) {
                    const double wy = std::min(v1, y + 1.0) - std::max(v0, double(y));
                    for (int x = std::max(x0, int(std::floor(u0))); x < std::min(x1, int(std::ceil(u1))); x++) {
                        const double w = wy * (std::min(u1, x + 1.0) - std::max(u0, double(x)));
                        double c[3];
                        const double a = pixel(x, y, c);
                        for (int ch = 0; ch < 3; ch++) sum[ch] += c[ch] * w;
                        alpha += a * w;
                    }
                }
                const double area = (u1 - u0) * (v1 - v0);
                for (double& s : sum) s /= area;
                alpha /= area;
            } else {
                const double u = (X + 0.5 - ox) / scale + x0 - 0.5, v = (Y + 0.5 - oy) / scale + y0 - 0.5;
                const int ix = int(std::floor(u)), iy = int(std::floor(v));
                const double fx = u - ix, fy = v - iy;
                for (int k = 0; k < 4; k++) {
                    const int dx = k & 1, dy = k >> 1;
                    const double w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                    double c[3];
                    const double a = pixel(ix + dx, iy + dy, c);
                    for (int ch = 0; ch < 3; ch++) sum[ch] += c[ch] * w;
                    alpha += a * w;
                }
            }
            uint8_t* o = out.At(X, Y);
            if (alpha <= 0.0) continue;
            for (int ch = 0; ch < 3; ch++) o[ch] = detail::Byte(sum[ch] / alpha);
            o[3] = detail::Byte(alpha * 255.0);
        }
    }
    return out;
}

// The sizes the icon carries, largest first. The logo is dark and thin, and below 64 pixels it all but vanishes on a dark
// taskbar, so there the save image, the game's own small picture on grey, takes over when the XBE has one.
inline std::vector<Image> IconSizes(const Image& logo, const std::optional<Image>& save) {
    std::vector<Image> out;
    for (int size : {256, 128, 64, 48, 32, 24, 16}) out.push_back(save && size <= 48 ? FitSquare(*save, size, 1.0) : FitSquare(logo, size));
    return out;
}

// An .ico with one 32-bit entry per image, in the order given: rows from the bottom, then a mask of the clear pixels
inline std::vector<uint8_t> Ico(const std::vector<Image>& images) {
    using detail::Write16, detail::Write32;
    std::vector<uint8_t> out;
    Write16(out, 0);
    Write16(out, 1);
    Write16(out, uint16_t(images.size()));
    auto bytes = [](const Image& im) { return uint32_t(40 + im.w * im.h * 4 + (im.w + 31) / 32 * 4 * im.h); };
    uint32_t at = uint32_t(6 + 16 * images.size());
    for (const Image& im : images) {
        out.push_back(uint8_t(im.w >= 256 ? 0 : im.w));
        out.push_back(uint8_t(im.h >= 256 ? 0 : im.h));
        out.push_back(0);
        out.push_back(0);
        Write16(out, 1);
        Write16(out, 32);
        Write32(out, bytes(im));
        Write32(out, at);
        at += bytes(im);
    }
    for (const Image& im : images) {
        const uint32_t stride = (im.w + 31) / 32 * 4;
        for (uint32_t v : {40u, uint32_t(im.w), uint32_t(im.h * 2)}) Write32(out, v);
        Write16(out, 1);
        Write16(out, 32);
        for (uint32_t v : {0u, uint32_t(im.w * im.h * 4 + stride * im.h), 0u, 0u, 0u, 0u}) Write32(out, v);
        for (int y = im.h - 1; y >= 0; y--)
            for (int x = 0; x < im.w; x++) {
                const uint8_t* p = im.At(x, y);
                out.insert(out.end(), {p[2], p[1], p[0], p[3]});
            }
        for (int y = im.h - 1; y >= 0; y--) {
            std::vector<uint8_t> row(stride, 0);
            for (int x = 0; x < im.w; x++)
                if (im.At(x, y)[3] == 0) row[x / 8] |= uint8_t(0x80 >> (x % 8));
            out.insert(out.end(), row.begin(), row.end());
        }
    }
    return out;
}

}  // namespace xbe_icon
