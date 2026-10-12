#ifndef FS_FAT_FAT_SHORT_NAME_HPP
#define FS_FAT_FAT_SHORT_NAME_HPP

#include "infra/util/ByteRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fs
{
    struct ShortName
    {
        static constexpr uint8_t lowerCaseBase = 0x08;
        static constexpr uint8_t lowerCaseExtension = 0x10;
        static constexpr std::size_t maxUtf8Bytes = 34;

        std::array<uint8_t, 11> bytes{};
        uint8_t caseFlags{ 0 };

        bool operator==(const ShortName& other) const = default;
    };

    uint8_t LongNameChecksum(const std::array<uint8_t, 11>& name);
    std::size_t ShortNameToUtf8(const ShortName& name, infra::ByteRange out);
}

#endif
