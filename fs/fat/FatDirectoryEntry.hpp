#ifndef FS_FAT_FAT_DIRECTORY_ENTRY_HPP
#define FS_FAT_FAT_DIRECTORY_ENTRY_HPP

#include "fs/fat/FatShortName.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatAttributes
    {
        static constexpr uint8_t readOnly = 0x01;
        static constexpr uint8_t hidden = 0x02;
        static constexpr uint8_t system = 0x04;
        static constexpr uint8_t volumeId = 0x08;
        static constexpr uint8_t directory = 0x10;
        static constexpr uint8_t archive = 0x20;
        static constexpr uint8_t longName = 0x0f;
    };

    enum class EntryKind : uint8_t
    {
        end,
        free,
        longName,
        volumeLabel,
        dot,
        directory,
        file
    };

    struct ShortEntry
    {
        ShortName name;
        uint8_t attributes{ 0 };
        uint16_t createTime{ 0 };
        uint16_t createDate{ 0 };
        uint16_t accessDate{ 0 };
        uint16_t writeTime{ 0 };
        uint16_t writeDate{ 0 };
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };

        bool operator==(const ShortEntry& other) const = default;
    };

    struct LongEntry
    {
        static constexpr std::size_t units = 13;
        static constexpr uint8_t maxOrdinal = 20;

        uint8_t ordinal{ 0 };
        bool last{ false };
        uint8_t checksum{ 0 };
        std::array<char16_t, units> name{};

        bool operator==(const LongEntry& other) const = default;
    };

    constexpr std::size_t directoryEntryBytes = 32;

    EntryKind KindOf(infra::ConstByteRange entry);
    ShortEntry DecodeShortEntry(infra::ConstByteRange entry);
    void EncodeShortEntry(const ShortEntry& entry, infra::ByteRange out);
    std::optional<LongEntry> DecodeLongEntry(infra::ConstByteRange entry);
    void EncodeLongEntry(infra::BoundedConstString name, uint8_t ordinal, bool last, uint8_t checksum, infra::ByteRange out);
    uint8_t LongEntriesFor(infra::BoundedConstString name);
}

#endif
