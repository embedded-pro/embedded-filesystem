#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/Utf8.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <utility>

namespace fs
{
    bool FatEntryPosition::operator==(const FatEntryPosition& other) const = default;

    bool IsDirectory(const FatScannedEntry& entry)
    {
        return (entry.entry.attributes & FatAttributes::directory) != 0;
    }

    EntryInfo InfoOf(const FatScannedEntry& entry)
    {
        if (IsDirectory(entry))
            return EntryInfo{ entry.name, EntryType::directory, 0 };

        return EntryInfo{ entry.name, EntryType::file, entry.entry.size };
    }

    namespace
    {
        constexpr char16_t terminator = 0x0000;
        constexpr char16_t padding = 0xffff;
    }

    FatEntryAssembler::FatEntryAssembler(infra::ByteRange nameBuffer)
        : nameBuffer(nameBuffer)
        , nameBegin(nameBuffer.size())
    {
        really_assert(nameBuffer.size() >= ShortName::maxUtf8Bytes);
    }

    FatEntryAssembler::Outcome FatEntryAssembler::Feed(infra::ConstByteRange entry, FatEntryPosition position)
    {
        switch (KindOf(entry))
        {
            case EntryKind::end:
                Reset();
                return Outcome::end;
            case EntryKind::longName:
                FeedLongEntry(DecodeLongEntry(entry), position);
                return Outcome::none;
            case EntryKind::file:
            case EntryKind::directory:
                FeedShortEntry(entry, position);
                return Outcome::entry;
            default:
                Reset();
                return Outcome::none;
        }
    }

    const FatScannedEntry& FatEntryAssembler::Entry() const
    {
        return scanned;
    }

    void FatEntryAssembler::Reset()
    {
        nameBegin = nameBuffer.size();
        expectedOrdinal = std::nullopt;
        pendingLowSurrogate = std::nullopt;
        invalid = false;
    }

    void FatEntryAssembler::FeedLongEntry(const std::optional<LongEntry>& entry, FatEntryPosition position)
    {
        if (!entry)
            Reset();
        else if (entry->last)
            StartSet(*entry, position);
        else if (expectedOrdinal && entry->ordinal + 1 == *expectedOrdinal && entry->checksum == checksum)
        {
            expectedOrdinal = entry->ordinal;
            ++scanned.slots;
            Prepend(*entry);
        }
        else
            Reset();
    }

    void FatEntryAssembler::StartSet(const LongEntry& entry, FatEntryPosition position)
    {
        Reset();
        expectedOrdinal = entry.ordinal;
        checksum = entry.checksum;
        scanned.first = position;
        scanned.slots = 1;
        Prepend(entry);
    }

    void FatEntryAssembler::Prepend(const LongEntry& entry)
    {
        bool inName = false;

        for (auto unit = entry.name.rbegin(); unit != entry.name.rend(); ++unit)
        {
            if (*unit == padding && !inName)
                continue;

            if (*unit == terminator && !inName)
            {
                inName = true;
                continue;
            }

            inName = true;

            if (IsLowSurrogate(*unit) && !pendingLowSurrogate)
                pendingLowSurrogate = *unit;
            else if (IsHighSurrogate(*unit) && pendingLowSurrogate)
                PrependCodePoint(CombineSurrogates(*unit, *std::exchange(pendingLowSurrogate, std::nullopt)));
            else if (IsLowSurrogate(*unit) || IsHighSurrogate(*unit) || pendingLowSurrogate || *unit == terminator)
                invalid = true;
            else
                PrependCodePoint(*unit);
        }
    }

    void FatEntryAssembler::PrependCodePoint(char32_t codePoint)
    {
        auto bytes = Utf8Bytes(codePoint);

        if (bytes > nameBegin)
        {
            invalid = true;
            return;
        }

        nameBegin -= bytes;
        EncodeUtf8(codePoint, infra::DiscardHead(nameBuffer, nameBegin));
    }

    void FatEntryAssembler::FeedShortEntry(infra::ConstByteRange entry, FatEntryPosition position)
    {
        scanned.entry = DecodeShortEntry(entry);
        scanned.shortEntry = position;
        scanned.longName = LongNameCompletes(scanned.entry);

        if (scanned.longName)
        {
            ++scanned.slots;
            scanned.name = infra::BoundedConstString(reinterpret_cast<const char*>(nameBuffer.begin() + nameBegin), nameBuffer.size() - nameBegin);
        }
        else
        {
            scanned.first = position;
            scanned.slots = 1;
            auto size = ShortNameToUtf8(scanned.entry.name, nameBuffer);
            scanned.name = infra::BoundedConstString(reinterpret_cast<const char*>(nameBuffer.begin()), size);
        }

        Reset();
    }

    bool FatEntryAssembler::LongNameCompletes(const ShortEntry& entry) const
    {
        return expectedOrdinal == 1 && !invalid && !pendingLowSurrogate && nameBegin != nameBuffer.size() && LongNameChecksum(entry.name.bytes) == checksum;
    }
}
