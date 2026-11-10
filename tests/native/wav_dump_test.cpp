#include <cstdint>
#include <cstring>

#include "audio/wav_dump.h"
#include "check.h"

static uint16_t U16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

static uint32_t U32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

int main() {
    auto h = audio::FloatWavHeader(48000, 2);
    CHECK(h.size() == 44);
    CHECK(std::memcmp(h.data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(h.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(U32(h.data() + 16) == 16);
    CHECK(U16(h.data() + 20) == 3);  // IEEE float
    CHECK(U16(h.data() + 22) == 2);
    CHECK(U32(h.data() + 24) == 48000);
    CHECK(U32(h.data() + 28) == 48000 * 8);
    CHECK(U16(h.data() + 32) == 8);
    CHECK(U16(h.data() + 34) == 32);
    CHECK(std::memcmp(h.data() + 36, "data", 4) == 0);
    CHECK(U32(h.data() + 40) == 0);  // the game may be killed: the reader takes the data up to the end of the file
    return g_failures;
}
