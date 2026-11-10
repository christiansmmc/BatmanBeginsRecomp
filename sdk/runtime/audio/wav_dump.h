// BB_AUDIO_DUMP: the mixer's output as a WAV file, for checking sound without listening to it
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace audio {

// sizes stay 0: the file ends when the game does, the reader takes the data up to the end of the file
inline std::array<uint8_t, 44> FloatWavHeader(uint32_t rate, uint16_t channels) {
    std::array<uint8_t, 44> h{};
    const uint16_t block = channels * 4;
    const uint32_t bytes = rate * block;
    const uint32_t fmt_size = 16;
    const uint16_t tag = 3, bits = 32;
    std::memcpy(&h[0], "RIFF", 4);
    std::memcpy(&h[8], "WAVEfmt ", 8);
    std::memcpy(&h[16], &fmt_size, 4);
    std::memcpy(&h[20], &tag, 2);
    std::memcpy(&h[22], &channels, 2);
    std::memcpy(&h[24], &rate, 4);
    std::memcpy(&h[28], &bytes, 4);
    std::memcpy(&h[32], &block, 2);
    std::memcpy(&h[34], &bits, 2);
    std::memcpy(&h[36], "data", 4);
    return h;
}

}  // namespace audio
