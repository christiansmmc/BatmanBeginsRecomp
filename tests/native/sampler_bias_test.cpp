#include <cmath>
#include <limits>

#include "check.h"
#include "gpu/sampler_bias.h"

int main() {
    // what this PC's Direct3D 12 takes unchanged, measured in runs/dred-probe: 15.99 and -16 keep the device
    CHECK(gpu::SamplerBias(0.0f) == 0.0f);
    CHECK(gpu::SamplerBias(-1.5f) == -1.5f);
    CHECK(gpu::SamplerBias(15.99f) == 15.99f);
    CHECK(gpu::SamplerBias(-16.0f) == -16.0f);

    // and what removes the device: the title's values may be anything, and a console takes them
    CHECK(gpu::SamplerBias(16.0f) == 15.99f);
    CHECK(gpu::SamplerBias(1000.0f) == 15.99f);
    CHECK(gpu::SamplerBias(-16.01f) == -16.0f);
    CHECK(gpu::SamplerBias(std::numeric_limits<float>::infinity()) == 15.99f);
    CHECK(gpu::SamplerBias(-std::numeric_limits<float>::infinity()) == -16.0f);
    CHECK(gpu::SamplerBias(std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    // a value that had to change is told apart, for the log
    CHECK(!gpu::SamplerBiasRefused(15.99f));
    CHECK(!gpu::SamplerBiasRefused(-16.0f));
    CHECK(gpu::SamplerBiasRefused(16.0f));
    CHECK(gpu::SamplerBiasRefused(-16.01f));
    CHECK(gpu::SamplerBiasRefused(std::numeric_limits<float>::quiet_NaN()));
    return g_failures;
}
