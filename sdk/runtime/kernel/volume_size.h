// The size the hard disk partitions report to a title: the data partition of the console's 8 GB disk, empty. A title may
// keep only the low half of the free bytes (Batman Begins at 0x20C069), so an exact multiple of 4 GiB reads as a full disk.
#pragma once
#include <cstdint>

namespace kernel::volume {

inline constexpr uint32_t kBytesPerSector = 512;
inline constexpr uint32_t kSectorsPerUnit = 32;  // 16 KiB, the save game block
inline constexpr uint64_t kBytesPerUnit = kBytesPerSector * kSectorsPerUnit;
inline constexpr uint64_t kTotalUnits = 0x1312D6000 / kBytesPerUnit;
inline constexpr uint64_t kFreeUnits = kTotalUnits;

}  // namespace kernel::volume
