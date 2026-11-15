#include <cstdio>
#include <cstdlib>

#include "check.h"
#include "common/prompt_art.h"
#include "gpu/bc_decode.h"

using namespace prompts;

namespace {

Bitmap Flat(uint32_t w, uint32_t h, uint8_t v) {
    Bitmap b;
    b.w = w;
    b.h = h;
    b.px.assign(size_t(w) * h * 4, v);
    return b;
}

uint8_t Alpha(const Bitmap& b, int x, int y) { return b.px[(size_t(y) * b.w + size_t(x)) * 4 + 3]; }

void TestLabels() {
    CHECK(KeyLabel('W') == "W");
    CHECK(KeyLabel('7') == "7");
    CHECK(KeyLabel(0x20) == "SPC");
    CHECK(KeyLabel(0xA0) == "LSH");
    CHECK(KeyLabel(0xA3) == "RCT");
    CHECK(KeyLabel(0x01) == "LMB");
    CHECK(KeyLabel(0x04) == "MMB");
    CHECK(KeyLabel(0x74) == "F5");
    CHECK(KeyLabel(0x26) == "^");
    CHECK(KeyLabel(0xFFFF) == "?");
    // every label is drawable and at most three characters
    for (uint32_t vk = 0; vk < 256; vk++) {
        const std::string l = KeyLabel(vk);
        CHECK(!l.empty() && l.size() <= 3);
        for (char c : l) CHECK(GlyphOf(c).c == c || c == '?');
    }
}

void TestSheetsAreInside() {
    for (const Sheet& s : kSheets) {
        for (size_t i = 0; i < s.count; i++) {
            const Rect& r = s.cells[i].box;
            CHECK(r.x0 >= 1 && r.y0 >= 1 && r.x1 < int(s.w) - 1 && r.y1 < int(s.h) - 1);  // room for the margin
            CHECK(r.x1 > r.x0 && r.y1 > r.y0);
        }
    }
    CHECK(FindSheet(0xe17fcca2edd00edbull) != nullptr);
    CHECK(FindSheet(1) == nullptr);
}

void TestPaint() {
    const Sheet& font = *FindSheet(0xe17fcca2edd00edbull);
    const Bitmap src = Flat(128, 128, 77);
    const Bitmap out = PaintSheet(font, kDefaultKeyMap, src);
    CHECK(out.w == 512 && out.h == 512);
    // the middle of the A button holds a key cap now, the corner of the texture is what it was
    CHECK(Alpha(out, (1 + 10) * 4, (3 + 9) * 4) == 255);
    CHECK(out.px[3] == 77);
    // far from any button it is what it was
    CHECK(Alpha(out, 511, 511) == 77);

    if (const char* path = std::getenv("PROMPT_ART_OUT")) {
        // the picture on a grey ground, as a PPM
        std::FILE* f = std::fopen(path, "wb");
        if (f) {
            std::fprintf(f, "P6\n%u %u\n255\n", out.w, out.h);
            for (size_t i = 0; i < size_t(out.w) * out.h; i++) {
                const uint8_t* p = &out.px[i * 4];
                const float a = float(p[3]) / 255.0f;
                const uint8_t rgb[3] = {uint8_t(p[2] * a + 70 * (1 - a)), uint8_t(p[1] * a + 70 * (1 - a)), uint8_t(p[0] * a + 70 * (1 - a))};
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
        }
    }
}

void TestBc() {
    // one BC1 block: c0 = pure red (0xF800), c1 = pure blue (0x001F), every index 0, then every index 1
    const uint8_t red[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    std::vector<uint8_t> px = gpu::DecodeBcToBgra(gpu::Bc::Bc1, 4, 4, red);
    CHECK(px.size() == 64);
    CHECK(px[0] == 0 && px[1] == 0 && px[2] == 255 && px[3] == 255);
    const uint8_t blue[8] = {0x00, 0xF8, 0x1F, 0x00, 0x55, 0x55, 0x55, 0x55};
    px = gpu::DecodeBcToBgra(gpu::Bc::Bc1, 4, 4, blue);
    CHECK(px[0] == 255 && px[2] == 0);
    // BC2: explicit 4 bit alpha, pixel 0 is 0xF, pixel 1 is 0x0
    uint8_t dxt3[16] = {0x0F};
    std::copy(red, red + 8, dxt3 + 8);
    px = gpu::DecodeBcToBgra(gpu::Bc::Bc2, 4, 4, dxt3);
    CHECK(px[3] == 255 && px[7] == 0);
    // BC3: alpha endpoints 255 and 0, index 0 for pixel 0 and 1 for pixel 1
    uint8_t dxt5[16] = {0xFF, 0x00, 0x08};
    std::copy(red, red + 8, dxt5 + 8);
    px = gpu::DecodeBcToBgra(gpu::Bc::Bc3, 4, 4, dxt5);
    CHECK(px[3] == 255 && px[7] == 0);
}

}  // namespace

int main() {
    TestLabels();
    TestSheetsAreInside();
    TestPaint();
    TestBc();
    return g_failures;
}
