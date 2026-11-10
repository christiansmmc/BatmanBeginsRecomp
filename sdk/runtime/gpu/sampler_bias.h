// A title's mip bias may be any float. Direct3D 12 removes the whole device for a sampler with one out of its range, with nothing
// to say why: CreateSampler returns nothing. These are D3D12_MIP_LOD_BIAS_MIN and _MAX, and at either end the level no longer moves.
#pragma once

namespace gpu {

inline constexpr float kMipBiasMin = -16.0f;
inline constexpr float kMipBiasMax = 15.99f;

inline constexpr bool SamplerBiasRefused(float bias) { return !(bias >= kMipBiasMin && bias <= kMipBiasMax); }

inline constexpr float SamplerBias(float bias) {
    if (bias != bias) return 0.0f;  // not a number
    if (bias < kMipBiasMin) return kMipBiasMin;
    if (bias > kMipBiasMax) return kMipBiasMax;
    return bias;
}

}  // namespace gpu
