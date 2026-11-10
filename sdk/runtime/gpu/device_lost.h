// Which results of IDXGISwapChain::Present mean the graphics device is gone: no frame will ever show again, though the window
// and the sound go on, and to a player that looks like a frozen PC.
#pragma once
#include <cstdint>

namespace gpu {

inline constexpr bool DeviceLost(int32_t present_result) {
    switch (uint32_t(present_result)) {
        case 0x887A0005u:  // DXGI_ERROR_DEVICE_REMOVED
        case 0x887A0006u:  // DXGI_ERROR_DEVICE_HUNG
        case 0x887A0007u:  // DXGI_ERROR_DEVICE_RESET
            return true;
        default:
            return false;
    }
}

}  // namespace gpu
