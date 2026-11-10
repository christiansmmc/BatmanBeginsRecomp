#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <winerror.h>

#include "check.h"
#include "gpu/device_lost.h"

int main() {
    // the three Present returns that mean no frame will ever show again
    CHECK(gpu::DeviceLost(DXGI_ERROR_DEVICE_REMOVED));
    CHECK(gpu::DeviceLost(DXGI_ERROR_DEVICE_RESET));
    CHECK(gpu::DeviceLost(DXGI_ERROR_DEVICE_HUNG));

    // everything else keeps the game going: a success, a status that is not a failure, and other failures
    CHECK(!gpu::DeviceLost(S_OK));
    CHECK(!gpu::DeviceLost(DXGI_STATUS_OCCLUDED));
    CHECK(!gpu::DeviceLost(E_FAIL));
    CHECK(!gpu::DeviceLost(E_INVALIDARG));
    CHECK(!gpu::DeviceLost(E_OUTOFMEMORY));
    CHECK(!gpu::DeviceLost(DXGI_ERROR_INVALID_CALL));
    CHECK(!gpu::DeviceLost(DXGI_ERROR_WAS_STILL_DRAWING));
    CHECK(!gpu::DeviceLost(DXGI_ERROR_ACCESS_LOST));
    return g_failures;
}
