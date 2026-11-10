#include <cstdint>

#include "check.h"
#include "kernel/volume_size.h"

using namespace kernel::volume;

namespace {

constexpr uint64_t kRetailDataPartitionBytes = 0x1312D6000;  // E: on the console's 8 GB disk
constexpr int32_t kSaveGameBlocks = 3;                       // what Batman Begins asks for

// 0x20C069 in Batman Begins, the free blocks after a save: it reads only the low half of GetDiskFreeSpaceEx's free bytes
int32_t FreeBlocksAfterSave(uint64_t free_bytes) {
    uint32_t low = uint32_t(free_bytes);
    return int32_t(((low + 0x3FFFu) >> 14) - uint32_t(kSaveGameBlocks));
}

}  // namespace

int main() {
    uint64_t free_bytes = kFreeUnits * kBytesPerUnit;
    CHECK(FreeBlocksAfterSave(free_bytes) >= 0);
    CHECK(kBytesPerUnit == 0x4000);
    CHECK(kFreeUnits <= kTotalUnits);
    CHECK(kTotalUnits * kBytesPerUnit <= kRetailDataPartitionBytes);
    CHECK(kTotalUnits * kBytesPerUnit > kRetailDataPartitionBytes - kBytesPerUnit);
    return g_failures;
}
