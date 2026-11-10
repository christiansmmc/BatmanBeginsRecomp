#include <string>

#include "check.h"
#include "gpu/shaders.h"

namespace {

gpu::PixelKey ProgrammableKeyReadingStage1(uint8_t flags) {
    gpu::PixelKey k;
    k.programmable = 1;
    k.tex_modes = 1u << 5;  // stage 1 projects a 2D texture
    k.tex[1].kind = gpu::kTex2D;
    k.tex[1].flags = flags;
    return k;
}

void TestDepthReadThroughColourFormatGivesItsBytes() {
    std::string depth = gpu::PixelHlsl(ProgrammableKeyReadingStage1(gpu::kTexDepthBytes));
    CHECK(depth.find("t1 = depthbytes(t1.r);") != std::string::npos);
    std::string colour = gpu::PixelHlsl(ProgrammableKeyReadingStage1(0));
    CHECK(colour.find("t1 = depthbytes(") == std::string::npos);
}

// a w buffer's memory holds w itself, however the host keeps it: 1 / w would give 255 in the high byte almost everywhere
void TestDepthBytesUndoTheReversedWBuffer() {
    std::string depth = gpu::PixelHlsl(ProgrammableKeyReadingStage1(gpu::kTexDepthBytes));
    CHECK(depth.find("zread.y / (stored / zread.x + 1)") != std::string::npos);
}

}  // namespace

int main() {
    TestDepthReadThroughColourFormatGivesItsBytes();
    TestDepthBytesUndoTheReversedWBuffer();
    return g_failures;
}
