#ifndef VALIDATION_FIRMWARE_LAYOUT_FAT_LAYOUT_HPP
#define VALIDATION_FIRMWARE_LAYOUT_FAT_LAYOUT_HPP

#include "fs/fat/FatFormatLayout.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/MemoryRange.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    enum class LayoutType : uint8_t
    {
        fat12,
        fat16,
        fat32,
        exfat
    };

    // Where a card holds its volume: the first partition of its master boot record, or the card from sector 0 on, with the
    // sectors that the boot sector gives
    struct VolumePlace
    {
        uint32_t start;
        uint32_t sectors;
    };

    struct FatLayout
    {
        uint32_t partitionStart{ 0 };
        uint32_t partitionSectors{ 0 };
        LayoutType type{ LayoutType::fat12 };
        uint32_t sectorsPerCluster{ 0 };
        uint32_t reservedSectors{ 0 };
        uint32_t fats{ 0 };
        uint32_t fatSectors{ 0 };
        uint32_t rootEntries{ 0 };
        uint32_t dataStart{ 0 };

        bool operator==(const FatLayout& other) const = default;
    };

    // The layout of a card is parsed here without the code of fs/fat, so that the formatter is not compared with its own parser
    std::optional<VolumePlace> LocateVolume(infra::ConstByteRange firstSector);
    std::optional<FatLayout> ParseVolume(infra::ConstByteRange bootSector, const VolumePlace& place);
    FatLayout FormatterLayout(const fs::FatFormatLayout& layout);
    std::size_t Differences(const FatLayout& card, const FatLayout& formatter, infra::BoundedString& names);
    const char* TypeName(LayoutType type);
    void AddLayout(ResultRecord& record, const FatLayout& layout);
}

#endif
