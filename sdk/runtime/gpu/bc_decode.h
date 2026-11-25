// BC1, BC2 and BC3 (DXT1, DXT3, DXT5) to plain pixels, for code that draws over a texture's bytes.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gpu {

enum class Bc { Bc1, Bc2, Bc3 };

namespace bc_detail {

inline void Colour565(uint16_t c, uint8_t out[3]) {  // r, g, b
    const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = uint8_t((r << 3) | (r >> 2));
    out[1] = uint8_t((g << 2) | (g >> 4));
    out[2] = uint8_t((b << 3) | (b >> 2));
}

}  // namespace bc_detail

// `data` is the level's blocks, a block row after another; the result is w * h * 4 bytes, B G R A, rows tightly packed.
// A size that is not a multiple of 4 reads the blocks that cover it.
inline std::vector<uint8_t> DecodeBcToBgra(Bc kind, uint32_t w, uint32_t h, const uint8_t* data) {
    std::vector<uint8_t> out(size_t(w) * h * 4);
    const uint32_t blocks_x = (w + 3) / 4, blocks_y = (h + 3) / 4;
    const size_t block_bytes = kind == Bc::Bc1 ? 8 : 16;
    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint8_t* b = data + (size_t(by) * blocks_x + bx) * block_bytes;
            const uint8_t* cb = kind == Bc::Bc1 ? b : b + 8;
            const uint16_t c0 = uint16_t(cb[0] | cb[1] << 8), c1 = uint16_t(cb[2] | cb[3] << 8);
            uint8_t pal[4][4];  // r, g, b, a
            bc_detail::Colour565(c0, pal[0]);
            bc_detail::Colour565(c1, pal[1]);
            pal[0][3] = pal[1][3] = pal[2][3] = 255;
            pal[3][3] = 255;
            const bool four = kind != Bc::Bc1 || c0 > c1;
            for (int c = 0; c < 3; c++) {
                if (four) {
                    pal[2][c] = uint8_t((2 * pal[0][c] + pal[1][c]) / 3);
                    pal[3][c] = uint8_t((pal[0][c] + 2 * pal[1][c]) / 3);
                } else {
                    pal[2][c] = uint8_t((pal[0][c] + pal[1][c]) / 2);
                    pal[3][c] = 0;
                }
            }
            if (!four) pal[3][3] = 0;
            const uint32_t indices = uint32_t(cb[4]) | uint32_t(cb[5]) << 8 | uint32_t(cb[6]) << 16 | uint32_t(cb[7]) << 24;

            uint8_t alpha[16];
            if (kind == Bc::Bc2) {
                for (int i = 0; i < 16; i++) {
                    const uint8_t nibble = (b[i / 2] >> ((i & 1) * 4)) & 15;
                    alpha[i] = uint8_t(nibble * 17);
                }
            } else if (kind == Bc::Bc3) {
                uint8_t a[8] = {b[0], b[1]};
                if (a[0] > a[1]) {
                    for (int i = 1; i < 7; i++) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
                } else {
                    for (int i = 1; i < 5; i++) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5);
                    a[6] = 0;
                    a[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = a[(bits >> (3 * i)) & 7];
            } else {
                std::fill(alpha, alpha + 16, uint8_t(255));
            }

            for (uint32_t py = 0; py < 4; py++) {
                for (uint32_t px = 0; px < 4; px++) {
                    const uint32_t x = bx * 4 + px, y = by * 4 + py;
                    if (x >= w || y >= h) continue;
                    const uint32_t i = py * 4 + px;
                    const uint8_t* c = pal[(indices >> (2 * i)) & 3];
                    uint8_t* o = &out[(size_t(y) * w + x) * 4];
                    o[0] = c[2];
                    o[1] = c[1];
                    o[2] = c[0];
                    o[3] = kind == Bc::Bc1 ? c[3] : alpha[i];
                }
            }
        }
    }
    return out;
}

}  // namespace gpu
