// D3DDevice_SetVertexShaderInputDirect's arguments as the renderer's vertex layout. The library programs the hardware
// from them: attribute address = buffer data + stream offset + attribute offset, format = stride << 8 | type.
#pragma once
#include <array>
#include <cstdint>

#include "gpu/state.h"

namespace d3d {

struct VertexInputLayout {
    std::array<gpu::VertexShader::Input, 16> inputs{};
    std::array<gpu::State::Stream, 16> streams{};
};

// vaf: D3DVERTEXATTRIBUTEFORMAT, 16 inputs of 4 dwords (stream, offset, format, tessellation)
// stream_inputs: D3DSTREAM_INPUT of 3 dwords (vertex buffer, stride, offset), read by each input's stream index as the
// library does: a title may leave empty streams in the array and not count them
inline VertexInputLayout ParseVertexInput(const uint32_t* vaf, const uint32_t* stream_inputs) {
    constexpr uint32_t kTypeNone = 0x02;
    constexpr uint32_t kTypeMask = 0xFF;
    VertexInputLayout layout;
    for (uint32_t reg = 0; reg < layout.inputs.size(); reg++) {
        const uint32_t stream = vaf[reg * 4], offset = vaf[reg * 4 + 1], type = vaf[reg * 4 + 2] & kTypeMask;
        if (type == kTypeNone || !type || stream >= layout.streams.size()) continue;
        layout.inputs[reg] = {int(stream), offset + stream_inputs[stream * 3 + 2], type};
        layout.streams[stream] = {stream_inputs[stream * 3], stream_inputs[stream * 3 + 1]};
    }
    return layout;
}

}  // namespace d3d
