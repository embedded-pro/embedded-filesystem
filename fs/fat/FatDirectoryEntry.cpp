#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatBytes.hpp"
#include "fs/fat/Utf8.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr std::size_t nameBytes = 11;
        constexpr std::size_t attributesOffset = 11;
        constexpr std::size_t caseOffset = 12;
        constexpr std::size_t checksumOffset = 13;
        constexpr uint8_t endMarker = 0x00;
        constexpr uint8_t deletedMarker = 0xe5;
        constexpr uint8_t lastFlag = 0x40;
        constexpr uint8_t ordinalMask = 0x1f;
        constexpr uint8_t longNameMask = 0x3f;
        constexpr char16_t terminator = 0x0000;
        constexpr char16_t padding = 0xffff;
        constexpr std::array<std::size_t, LongEntry::units> unitOffsets{ 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

        void Check(infra::ConstByteRange entry)
        {
            really_assert(entry.size() >= directoryEntryBytes);
        }

        void SkipUnits(Utf16Encoder& encoder, std::size_t units)
        {
            for (std::size_t unit = 0; unit != units; ++unit)
                encoder.Next();
        }
    }

    EntryKind KindOf(infra::ConstByteRange entry)
    {
        Check(entry);

        auto attributes = entry[attributesOffset];

        if (entry[0] == endMarker)
            return EntryKind::end;
        if (entry[0] == deletedMarker)
            return EntryKind::free;
        if ((attributes & longNameMask) == FatAttributes::longName)
            return EntryKind::longName;
        if ((attributes & FatAttributes::volumeId) != 0)
            return EntryKind::volumeLabel;
        if (entry[0] == '.')
            return EntryKind::dot;
        if ((attributes & FatAttributes::directory) != 0)
            return EntryKind::directory;

        return EntryKind::file;
    }

    ShortEntry DecodeShortEntry(infra::ConstByteRange entry)
    {
        Check(entry);

        ShortEntry decoded;
        std::copy_n(entry.begin(), nameBytes, decoded.name.bytes.begin());
        decoded.name.caseFlags = entry[caseOffset] & (ShortName::lowerCaseBase | ShortName::lowerCaseExtension);
        decoded.attributes = entry[attributesOffset];
        decoded.createTime = LoadLittle16(entry, 14);
        decoded.createDate = LoadLittle16(entry, 16);
        decoded.accessDate = LoadLittle16(entry, 18);
        decoded.firstCluster = (static_cast<uint32_t>(LoadLittle16(entry, 20)) << 16) | LoadLittle16(entry, 26);
        decoded.writeTime = LoadLittle16(entry, 22);
        decoded.writeDate = LoadLittle16(entry, 24);
        decoded.size = LoadLittle32(entry, 28);
        return decoded;
    }

    void EncodeShortEntry(const ShortEntry& entry, infra::ByteRange out)
    {
        Check(out);

        std::fill_n(out.begin(), directoryEntryBytes, uint8_t{ 0 });
        std::ranges::copy(entry.name.bytes, out.begin());
        out[attributesOffset] = entry.attributes;
        out[caseOffset] = entry.name.caseFlags;
        StoreLittle16(out, 14, entry.createTime);
        StoreLittle16(out, 16, entry.createDate);
        StoreLittle16(out, 18, entry.accessDate);
        StoreLittle16(out, 20, static_cast<uint16_t>(entry.firstCluster >> 16));
        StoreLittle16(out, 22, entry.writeTime);
        StoreLittle16(out, 24, entry.writeDate);
        StoreLittle16(out, 26, static_cast<uint16_t>(entry.firstCluster));
        StoreLittle32(out, 28, entry.size);
    }

    std::optional<LongEntry> DecodeLongEntry(infra::ConstByteRange entry)
    {
        if (KindOf(entry) != EntryKind::longName || entry[caseOffset] != 0)
            return std::nullopt;

        LongEntry decoded;
        decoded.ordinal = entry[0] & ordinalMask;
        decoded.last = (entry[0] & lastFlag) != 0;
        decoded.checksum = entry[checksumOffset];

        if (decoded.ordinal == 0 || decoded.ordinal > LongEntry::maxOrdinal)
            return std::nullopt;

        for (std::size_t unit = 0; unit != LongEntry::units; ++unit)
            decoded.name[unit] = LoadLittle16(entry, unitOffsets[unit]);

        return decoded;
    }

    void EncodeLongEntry(infra::BoundedConstString name, uint8_t ordinal, bool last, uint8_t checksum, infra::ByteRange out)
    {
        Check(out);
        really_assert(ordinal != 0 && ordinal <= LongEntry::maxOrdinal);

        std::fill_n(out.begin(), directoryEntryBytes, uint8_t{ 0 });
        out[0] = static_cast<uint8_t>(ordinal | (last ? lastFlag : 0));
        out[attributesOffset] = FatAttributes::longName;
        out[checksumOffset] = checksum;

        Utf16Encoder encoder{ name };
        SkipUnits(encoder, (ordinal - 1) * LongEntry::units);
        bool ended = false;

        for (auto offset : unitOffsets)
        {
            auto unit = encoder.Next();
            StoreLittle16(out, offset, unit.value_or(ended ? padding : terminator));
            ended = ended || !unit.has_value();
        }
    }

    uint8_t LongEntriesFor(infra::BoundedConstString name)
    {
        auto units = Utf16Units(name);
        really_assert(units.has_value());

        return static_cast<uint8_t>((*units + LongEntry::units - 1) / LongEntry::units);
    }
}
