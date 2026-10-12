#ifndef FS_FAT_FAT_ENTRY_ASSEMBLER_HPP
#define FS_FAT_FAT_ENTRY_ASSEMBLER_HPP

#include "fs/fat/FatDirectoryEntry.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatEntryPosition
    {
        uint32_t cluster{ 0 };
        uint32_t index{ 0 };

        bool operator==(const FatEntryPosition& other) const = default;
    };

    struct FatScannedEntry
    {
        ShortEntry entry;
        infra::BoundedConstString name;
        bool longName{ false };
        FatEntryPosition first;
        FatEntryPosition shortEntry;
        uint8_t slots{ 1 };
    };

    class FatEntryAssembler
    {
    public:
        enum class Outcome : uint8_t
        {
            none,
            entry,
            end
        };

        explicit FatEntryAssembler(infra::ByteRange nameBuffer);
        FatEntryAssembler(const FatEntryAssembler& other) = delete;
        FatEntryAssembler& operator=(const FatEntryAssembler& other) = delete;
        ~FatEntryAssembler() = default;

        Outcome Feed(infra::ConstByteRange entry, FatEntryPosition position);
        const FatScannedEntry& Entry() const;
        void Reset();

    private:
        void FeedLongEntry(const std::optional<LongEntry>& entry, FatEntryPosition position);
        void StartSet(const LongEntry& entry, FatEntryPosition position);
        void Prepend(const LongEntry& entry);
        void PrependCodePoint(char32_t codePoint);
        void FeedShortEntry(infra::ConstByteRange entry, FatEntryPosition position);
        bool LongNameCompletes(const ShortEntry& entry) const;

    private:
        infra::ByteRange nameBuffer;
        std::size_t nameBegin{ 0 };
        std::optional<uint8_t> expectedOrdinal;
        uint8_t checksum{ 0 };
        std::optional<char16_t> pendingLowSurrogate;
        bool invalid{ false };
        FatScannedEntry scanned;
    };
}

#endif
