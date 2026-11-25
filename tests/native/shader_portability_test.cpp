#include <string>
#include <vector>

#include "check.h"
#include "gpu/shaders.h"

namespace {

// 1.#INF, 1.#IND and 1.#QNAN are spellings only Microsoft's compiler reads. Wine's built-in d3dcompiler_47 (vkd3d)
// stops at them, which on Linux left every vertex shader uncompiled and the screen black
bool HasMsvcOnlyNumber(const std::string& hlsl) { return hlsl.find(".#") != std::string::npos; }

void TestFixedFunctionVertexShaderIsPortable() {
    CHECK(!HasMsvcOnlyNumber(gpu::FixedVertexHlsl(1, false)));
}

void TestVertexProgramShaderIsPortable() {
    // one instruction, the last: what it does matters less than the helpers every program carries
    std::vector<uint32_t> program = {0, 0, 0, 1};
    std::string hlsl = gpu::VertexProgramHlsl(program, 1);
    CHECK(hlsl.find("ilu_rsq") != std::string::npos);
    CHECK(!HasMsvcOnlyNumber(hlsl));
}

void TestPixelShaderIsPortable() {
    CHECK(!HasMsvcOnlyNumber(gpu::PixelHlsl(gpu::PixelKey{})));
}

}  // namespace

int main() {
    TestFixedFunctionVertexShaderIsPortable();
    TestVertexProgramShaderIsPortable();
    TestPixelShaderIsPortable();
    return g_failures;
}
