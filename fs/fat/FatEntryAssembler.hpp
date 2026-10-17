#ifndef FS_FAT_FAT_ENTRY_ASSEMBLER_HPP
#define FS_FAT_FAT_ENTRY_ASSEMBLER_HPP

#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/interfaces/EntryInfo.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatEntryPosition
    {
        uint32_t cluster;
        uint32_t index;

        bool operator==(const FatEntryPosition& other) const;
    };

    struct FatScannedEntry
    {
        ShortEntry entry;
        infra::BoundedConstString name;
        bool longName{ false };
        FatEntryPosition first{};
        FatEntryPosition shortEntry{};
        uint8_t slots{ 1 };
    };

    bool IsDirectory(const FatScannedEntry& entry);
    EntryInfo InfoOf(const FatScannedEntry& entry);
    bool NameMatches(const FatScannedEntry& entry, infra::BoundedConstString name);

    class FatEntryAssembler
    {
    public:
        enum class Outcome : uint8_t
        {
            none,
            entry,
            end
        };

        using Reading = Outcome (*)(FatEntryAssembler& assembler, infra::ConstByteRange entry, FatEntryPosition position);

        static Outcome ReadLongNames(FatEntryAssembler& assembler, infra::ConstByteRange entry, FatEntryPosition position);
        static Outcome ReadShortNames(FatEntryAssembler& assembler, infra::ConstByteRange entry, FatEntryPosition position);

        explicit FatEntryAssembler(infra::ByteRange nameBuffer, Reading reading = &ReadLongNames);
        FatEntryAssembler(const FatEntryAssembler& other) = delete;
        FatEntryAssembler& operator=(const FatEntryAssembler& other) = delete;
        ~FatEntryAssembler() = default;

        Outcome Feed(infra::ConstByteRange entry, FatEntryPosition position);
        const FatScannedEntry& Entry() const;
        void Reset();

    private:
        template<bool LongNames>
        Outcome Assemble(infra::ConstByteRange entry, FatEntryPosition position);
        void FeedLongEntry(const std::optional<LongEntry>& entry, FatEntryPosition position);
        void StartSet(const LongEntry& entry, FatEntryPosition position);
        void Prepend(const LongEntry& entry);
        void PrependCodePoint(char32_t codePoint);
        template<bool LongNames>
        void FeedShortEntry(infra::ConstByteRange entry, FatEntryPosition position);
        bool SetCompletes(const ShortEntry& entry) const;
        bool NameIsValid() const;

    private:
        infra::ByteRange nameBuffer;
        Reading reading;
        std::size_t nameBegin{ 0 };
        std::optional<uint8_t> expectedOrdinal;
        uint8_t checksum{ 0 };
        std::optional<char16_t> pendingLowSurrogate;
        bool invalid{ false };
        FatScannedEntry scanned;
    };
}

#endif
