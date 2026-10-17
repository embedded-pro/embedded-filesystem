#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/Utf8.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
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

    bool NameMatches(const FatScannedEntry& entry, infra::BoundedConstString name)
    {
        if (EqualIgnoringAsciiCase(entry.name, name))
            return true;

        if (!entry.longName)
            return false;

        std::array<uint8_t, ShortName::maxUtf8Bytes> shortName{};
        auto size = ShortNameToUtf8(entry.entry.name, infra::MakeRange(shortName));
        return EqualIgnoringAsciiCase(infra::BoundedConstString(reinterpret_cast<const char*>(shortName.data()), size), name);
    }

    namespace
    {
        constexpr char16_t terminator = 0x0000;
        constexpr char16_t padding = 0xffff;
    }

    FatEntryAssembler::Outcome FatEntryAssembler::ReadLongNames(FatEntryAssembler& assembler, infra::ConstByteRange entry, FatEntryPosition position)
    {
        return assembler.Assemble<true>(entry, position);
    }

    FatEntryAssembler::Outcome FatEntryAssembler::ReadShortNames(FatEntryAssembler& assembler, infra::ConstByteRange entry, FatEntryPosition position)
    {
        return assembler.Assemble<false>(entry, position);
    }

    FatEntryAssembler::FatEntryAssembler(infra::ByteRange nameBuffer, Reading reading)
        : nameBuffer(nameBuffer)
        , reading(reading)
        , nameBegin(nameBuffer.size())
    {
        really_assert(nameBuffer.size() >= ShortName::maxUtf8Bytes);
    }

    FatEntryAssembler::Outcome FatEntryAssembler::Feed(infra::ConstByteRange entry, FatEntryPosition position)
    {
        return reading(*this, entry, position);
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

    template<bool LongNames>
    FatEntryAssembler::Outcome FatEntryAssembler::Assemble(infra::ConstByteRange entry, FatEntryPosition position)
    {
        switch (KindOf(entry))
        {
            case EntryKind::end:
                Reset();
                return Outcome::end;
            case EntryKind::longName:
                if constexpr (LongNames)
                    FeedLongEntry(DecodeLongEntry(entry), position);
                return Outcome::none;
            case EntryKind::file:
            case EntryKind::directory:
                FeedShortEntry<LongNames>(entry, position);
                return Outcome::entry;
            default:
                Reset();
                return Outcome::none;
        }
    }

    void FatEntryAssembler::FeedLongEntry(const std::optional<LongEntry>& entry, FatEntryPosition position)
    {
        if (entry.has_value() && entry->last)
            StartSet(*entry, position);
        else if (entry.has_value() && expectedOrdinal.has_value() && entry->ordinal + 1 == *expectedOrdinal && entry->checksum == checksum)
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

            if (IsLowSurrogate(*unit) && !pendingLowSurrogate.has_value())
                pendingLowSurrogate = *unit;
            else if (IsHighSurrogate(*unit) && pendingLowSurrogate.has_value())
                PrependCodePoint(CombineSurrogates(*unit, *std::exchange(pendingLowSurrogate, std::nullopt)));
            else if (IsLowSurrogate(*unit) || IsHighSurrogate(*unit) || pendingLowSurrogate.has_value() || *unit == terminator)
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

    template<bool LongNames>
    void FatEntryAssembler::FeedShortEntry(infra::ConstByteRange entry, FatEntryPosition position)
    {
        scanned.entry = DecodeShortEntry(entry);
        scanned.shortEntry = position;

        if constexpr (LongNames)
        {
            auto complete = SetCompletes(scanned.entry);
            scanned.longName = complete && NameIsValid();

            if (complete)
                ++scanned.slots;
            else
            {
                scanned.first = position;
                scanned.slots = 1;
            }

            if (scanned.longName)
                scanned.name = infra::BoundedConstString(reinterpret_cast<const char*>(nameBuffer.begin() + nameBegin), nameBuffer.size() - nameBegin);
            else
            {
                auto size = ShortNameToUtf8(scanned.entry.name, nameBuffer);
                scanned.name = infra::BoundedConstString(reinterpret_cast<const char*>(nameBuffer.begin()), size);
            }
        }
        else
        {
            scanned.longName = false;
            scanned.first = position;
            scanned.slots = 1;
            auto size = ShortNameToUtf8(scanned.entry.name, nameBuffer);
            scanned.name = infra::BoundedConstString(reinterpret_cast<const char*>(nameBuffer.begin()), size);
        }

        Reset();
    }

    bool FatEntryAssembler::SetCompletes(const ShortEntry& entry) const
    {
        return expectedOrdinal == 1 && LongNameChecksum(entry.name.bytes) == checksum;
    }

    bool FatEntryAssembler::NameIsValid() const
    {
        return !invalid && !pendingLowSurrogate.has_value() && nameBegin != nameBuffer.size();
    }
}
