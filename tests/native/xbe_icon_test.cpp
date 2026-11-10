#include <cstdint>
#include <cstring>
#include <vector>

#include "check.h"
#include "xbe_icon.h"

namespace {

void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
uint32_t Get32(const std::vector<uint8_t>& b, size_t at) { uint32_t v; std::memcpy(&v, b.data() + at, 4); return v; }
uint16_t Get16(const std::vector<uint8_t>& b, size_t at) { uint16_t v; std::memcpy(&v, b.data() + at, 2); return v; }

// an XBE with two sections, .text and $$XTIMAGE, the image's 16 bytes at 0x400
std::vector<uint8_t> TwoSectionXbe() {
    std::vector<uint8_t> x(0x1000, 0);
    std::memcpy(x.data(), "XBEH", 4);
    Put32(x, 0x104, 0x10000);          // base address
    Put32(x, 0x11C, 2);                // sections
    Put32(x, 0x120, 0x10000 + 0x200);  // their headers
    const char* names[] = {".text", "$$XTIMAGE"};
    for (uint32_t i = 0; i < 2; i++) {
        const size_t h = 0x200 + i * 56, name = 0x300 + i * 0x10;
        std::memcpy(x.data() + name, names[i], std::strlen(names[i]) + 1);
        Put32(x, h + 12, i == 0 ? 0x800 : 0x400);  // raw address
        Put32(x, h + 16, 16);                      // raw size
        Put32(x, h + 20, 0x10000 + uint32_t(name));
    }
    for (int i = 0; i < 16; i++) x[0x400 + i] = uint8_t(0xA0 + i);
    return x;
}

// an XPR0 holding one DXT1 texture of w x h, its blocks after a 0x20 byte header
std::vector<uint8_t> Xpr(uint32_t format, int log_w, int log_h, const std::vector<uint8_t>& blocks) {
    std::vector<uint8_t> x(0x20 + blocks.size(), 0);
    std::memcpy(x.data(), "XPR0", 4);
    Put32(x, 4, uint32_t(x.size()));
    Put32(x, 8, 0x20);
    Put32(x, 12 + 12, (format << 8) | (1u << 16) | (uint32_t(log_w) << 20) | (uint32_t(log_h) << 24));
    std::memcpy(x.data() + 0x20, blocks.data(), blocks.size());
    return x;
}

std::vector<uint8_t> Block(uint16_t c0, uint16_t c1, uint32_t indices) {
    std::vector<uint8_t> b(8);
    std::memcpy(b.data(), &c0, 2);
    std::memcpy(b.data() + 2, &c1, 2);
    std::memcpy(b.data() + 4, &indices, 4);
    return b;
}

bool Is(const xbe_icon::Image& im, int x, int y, int r, int g, int b, int a) {
    const uint8_t* p = im.At(x, y);
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

xbe_icon::Image Filled(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    xbe_icon::Image im{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
    for (int i = 0; i < w * h; i++) {
        im.rgba[i * 4] = r, im.rgba[i * 4 + 1] = g, im.rgba[i * 4 + 2] = b, im.rgba[i * 4 + 3] = a;
    }
    return im;
}

void Set(xbe_icon::Image& im, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* p = im.At(x, y);
    p[0] = r, p[1] = g, p[2] = b, p[3] = a;
}

}  // namespace

int main() {
    // the section by its name, inside the file or not at all
    {
        const std::vector<uint8_t> x = TwoSectionXbe();
        auto image = xbe_icon::Section(x, "$$XTIMAGE");
        CHECK(image && image->size() == 16 && image->data() == x.data() + 0x400);
        CHECK(!xbe_icon::Section(x, "$$XSIMAGE"));
        CHECK(!xbe_icon::Section(std::span(x).first(0x150), "$$XTIMAGE"));  // headers cut off
        std::vector<uint8_t> past = x;
        Put32(past, 0x200 + 56 + 12, 0xFF8);  // raw data runs past the end
        CHECK(!xbe_icon::Section(past, "$$XTIMAGE"));
        std::vector<uint8_t> other = x;
        other[0] = 'Z';
        CHECK(!xbe_icon::Section(other, "$$XTIMAGE"));
    }

    // DXT1 with four colours: white, black, a third and two thirds of the way
    {
        auto im = xbe_icon::DecodeXpr(Xpr(0x0C, 2, 2, Block(0xFFFF, 0x0000, (1u << 2) | (2u << 4) | (3u << 6))));
        CHECK(im && im->w == 4 && im->h == 4);
        CHECK(im && Is(*im, 0, 0, 255, 255, 255, 255));
        CHECK(im && Is(*im, 1, 0, 0, 0, 0, 255));
        CHECK(im && Is(*im, 2, 0, 170, 170, 170, 255));
        CHECK(im && Is(*im, 3, 0, 85, 85, 85, 255));
        CHECK(im && Is(*im, 0, 3, 255, 255, 255, 255));
    }
    // and with three colours and a transparent one, when the first is not above the second
    {
        auto im = xbe_icon::DecodeXpr(Xpr(0x0C, 2, 2, Block(0x0000, 0xFFFF, (2u << 2) | (3u << 4))));
        CHECK(im && Is(*im, 0, 0, 0, 0, 0, 255));
        CHECK(im && Is(*im, 1, 0, 127, 127, 127, 255));
        CHECK(im && im->At(2, 0)[3] == 0);
    }
    // blocks go left to right, then down
    {
        std::vector<uint8_t> blocks = Block(0x0000, 0x0000, 0);
        std::vector<uint8_t> red = Block(0xF800, 0xF800, 0);
        blocks.insert(blocks.end(), red.begin(), red.end());
        blocks.resize(4 * 8, 0);
        auto im = xbe_icon::DecodeXpr(Xpr(0x0C, 3, 3, blocks));
        CHECK(im && im->w == 8 && im->h == 8);
        CHECK(im && Is(*im, 5, 1, 255, 0, 0, 255));
        CHECK(im && Is(*im, 1, 5, 0, 0, 0, 255));
    }
    // anything else is refused
    {
        CHECK(!xbe_icon::DecodeXpr(Xpr(0x06, 2, 2, std::vector<uint8_t>(64))));  // not DXT1
        CHECK(!xbe_icon::DecodeXpr(Xpr(0x0C, 3, 3, Block(0, 0, 0))));          // one block of four
        std::vector<uint8_t> other = Xpr(0x0C, 2, 2, Block(0, 0, 0));
        other[3] = '1';
        CHECK(!xbe_icon::DecodeXpr(other));
    }

    // the white around the picture goes, white inside it stays, and a light rim is unmixed from the white
    {
        xbe_icon::Image im = Filled(8, 8, 255, 255, 255, 255);
        for (int y = 2; y < 6; y++)
            for (int x = 2; x < 6; x++) Set(im, x, y, 0, 0, 0, 255);
        Set(im, 3, 3, 255, 255, 255, 255);  // enclosed
        Set(im, 0, 7, 240, 240, 240, 255);  // near white, at the edge
        Set(im, 6, 4, 200, 200, 200, 255);  // light grey on the picture's rim
        xbe_icon::ClearWhiteBackground(im);
        CHECK(im.At(0, 0)[3] == 0);
        CHECK(im.At(7, 7)[3] == 0);
        CHECK(im.At(0, 7)[3] == 0);
        CHECK(Is(im, 2, 2, 0, 0, 0, 255));
        CHECK(Is(im, 3, 3, 255, 255, 255, 255));
        const uint8_t* rim = im.At(6, 4);
        CHECK(rim[3] > 60 && rim[3] < 120);
        CHECK(rim[0] >= 90 && rim[0] <= 110);
    }

    // the opaque part fills the square's width, centred, a little in from the edges: 32 x 16 into 15 x 7.5
    {
        xbe_icon::Image im = Filled(64, 32, 0, 0, 0, 0);
        for (int y = 8; y < 24; y++)
            for (int x = 16; x < 48; x++) Set(im, x, y, 255, 0, 0, 255);
        xbe_icon::Image s = xbe_icon::FitSquare(im, 16);
        CHECK(s.w == 16 && s.h == 16);
        CHECK(s.At(0, 0)[3] == 0);
        CHECK(Is(s, 8, 8, 255, 0, 0, 255));
        CHECK(s.At(1, 8)[3] == 255);
        CHECK(s.At(0, 8)[3] > 0 && s.At(0, 8)[3] < 255);
        CHECK(s.At(8, 3)[3] == 0);
        CHECK(s.At(8, 5)[3] == 255);
        xbe_icon::Image big = xbe_icon::FitSquare(im, 64);
        CHECK(big.w == 64 && Is(big, 32, 32, 255, 0, 0, 255) && big.At(0, 0)[3] == 0);
        xbe_icon::Image empty = xbe_icon::FitSquare(Filled(4, 4, 9, 9, 9, 0), 16);
        CHECK(empty.w == 16 && empty.At(8, 8)[3] == 0);
    }

    // a picture meant to fill its square, the save image, is only scaled, with no margin
    {
        xbe_icon::Image s = xbe_icon::FitSquare(Filled(64, 64, 0, 255, 0, 255), 24, 1.0);
        CHECK(s.w == 24 && Is(s, 0, 0, 0, 255, 0, 255) && Is(s, 23, 23, 0, 255, 0, 255));
    }

    // the sizes the icon carries: the logo from 64 up; the save image, which stays legible on a dark taskbar, below that
    {
        xbe_icon::Image logo = Filled(8, 4, 0, 0, 255, 255);
        const std::vector<xbe_icon::Image> both = xbe_icon::IconSizes(logo, Filled(64, 64, 255, 0, 0, 255));
        CHECK(both.size() == 7);
        if (both.size() == 7) {
            const int want[] = {256, 128, 64, 48, 32, 24, 16};
            for (int i = 0; i < 7; i++) CHECK(both[i].w == want[i] && both[i].h == want[i]);
            CHECK(Is(both[0], 128, 128, 0, 0, 255, 255) && both[0].At(0, 0)[3] == 0);
            CHECK(Is(both[2], 32, 32, 0, 0, 255, 255));
            CHECK(Is(both[3], 0, 0, 255, 0, 0, 255));
            CHECK(Is(both[6], 15, 15, 255, 0, 0, 255));
        }
        const std::vector<xbe_icon::Image> logo_only = xbe_icon::IconSizes(logo, std::nullopt);
        CHECK(logo_only.size() == 7);
        if (logo_only.size() == 7) CHECK(logo_only[6].w == 16 && Is(logo_only[6], 8, 8, 0, 0, 255, 255) && logo_only[6].At(0, 0)[3] == 0);
    }

    // an icon file with 32-bit entries, rows from the bottom, and its mask
    {
        xbe_icon::Image small = Filled(16, 16, 0, 0, 0, 0);
        Set(small, 0, 0, 10, 20, 30, 40);
        const std::vector<uint8_t> ico = xbe_icon::Ico({small, Filled(256, 256, 1, 2, 3, 255)});
        const uint32_t size0 = 40 + 16 * 16 * 4 + 16 * 4, size1 = 40 + 256 * 256 * 4 + 256 * 32;
        const uint32_t at0 = 6 + 2 * 16, at1 = at0 + size0;
        CHECK(ico.size() == at1 + size1);
        if (ico.size() != at1 + size1) return g_failures;
        CHECK(Get16(ico, 0) == 0 && Get16(ico, 2) == 1 && Get16(ico, 4) == 2);
        CHECK(ico[6] == 16 && ico[7] == 16 && Get16(ico, 6 + 4) == 1 && Get16(ico, 6 + 6) == 32);
        CHECK(Get32(ico, 6 + 8) == size0 && Get32(ico, 6 + 12) == at0);
        CHECK(ico[22] == 0 && ico[23] == 0 && Get32(ico, 22 + 8) == size1 && Get32(ico, 22 + 12) == at1);
        CHECK(Get32(ico, at0) == 40 && Get32(ico, at0 + 4) == 16 && Get32(ico, at0 + 8) == 32 && Get16(ico, at0 + 14) == 32);
        const size_t top_left = at0 + 40 + (15 * 16 + 0) * 4;
        CHECK(ico[top_left] == 30 && ico[top_left + 1] == 20 && ico[top_left + 2] == 10 && ico[top_left + 3] == 40);
        const size_t mask = at0 + 40 + 16 * 16 * 4;
        CHECK((ico[mask + 15 * 4] & 0x80) == 0);  // top left is drawn
        CHECK((ico[mask + 15 * 4] & 0x40) != 0);  // its neighbour is not
    }
    return g_failures;
}
