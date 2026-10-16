#include "spike/fat_reader/FatReaderScan.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/fat/CodePage437.hpp"
#include "fs/fat/Utf8.hpp"

namespace spike
{
    namespace
    {
        constexpr std::size_t entryBytes = 32;
        constexpr uint32_t entriesPerSector = fatSectorBytes / entryBytes;
        constexpr uint32_t maxEntries = 65536;
        constexpr uint8_t endOfDirectory = 0x00;
        constexpr uint8_t freeEntry = 0xe5;
        constexpr uint8_t escapedFreeByte = 0x05;
        constexpr uint8_t longNameAttributes = 0x0f;
        constexpr uint8_t volumeLabel = 0x08;
        constexpr uint8_t directoryAttribute = 0x10;
        constexpr uint8_t lastLongEntry = 0x40;
        constexpr uint8_t ordinalMask = 0x1f;
        constexpr uint8_t maxOrdinal = 20;
        constexpr std::size_t unitsPerLongEntry = 13;
        constexpr std::array<uint8_t, unitsPerLongEntry> unitOffsets{ 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        constexpr uint8_t lowerBase = 0x08;
        constexpr uint8_t lowerExtension = 0x10;

        uint8_t ShortNameChecksum(infra::ConstByteRange entry)
        {
            uint8_t sum = 0;

            for (std::size_t index = 0; index != 11; ++index)
                sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + entry[index]);

            return sum;
        }

        bool Append(infra::BoundedString& text, char32_t codePoint)
        {
            auto bytes = fs::Utf8Bytes(codePoint);

            if (text.size() + bytes > text.max_size())
                return false;

            std::array<uint8_t, 4> encoded{};
            fs::EncodeUtf8(codePoint, infra::Head(infra::MakeRange(encoded), bytes));

            for (std::size_t index = 0; index != bytes; ++index)
                text.push_back(static_cast<char>(encoded[index]));

            return true;
        }

        void AppendShortPart(infra::BoundedString& text, infra::ConstByteRange part, bool lower)
        {
            auto length = part.size();

            while (length != 0 && part[length - 1] == ' ')
                --length;

            for (std::size_t index = 0; index != length; ++index)
            {
                uint8_t byte = part[index];

                if (lower && byte >= 'A' && byte <= 'Z')
                    byte = static_cast<uint8_t>(byte - 'A' + 'a');

                Append(text, fs::FromCodePage437(byte));
            }
        }
    }

    FatScan::FatScan(const FatLayout& layout, FatWindow& window, FatChain& chain)
        : layout(layout)
        , window(window)
        , chain(chain)
    {}

    FatPosition FatScan::Root() const
    {
        return FatPosition{ layout.kind == FatKind::fat32 ? layout.rootCluster : 0, 0, 0 };
    }

    void FatScan::Next(FatPosition& position, const infra::Function<void(fs::Result result, std::optional<FatEntry> entry)>& onDone)
    {
        this->position = &position;
        this->onDone = onDone;
        ForgetLongName();
        Load();
    }

    uint32_t FatScan::SectorOf(const FatPosition& at) const
    {
        if (at.cluster == 0)
            return layout.rootStart + at.sector;

        return layout.dataStart + ((at.cluster - 2) << layout.clusterShift) + at.sector;
    }

    void FatScan::Load()
    {
        if (position->entry >= maxEntries)
            End(fs::Result::success);
        else
            window.Load(SectorOf(*position), [this](fs::Result result)
                {
                    Loaded(result);
                });
    }

    void FatScan::Loaded(fs::Result result)
    {
        if (result != fs::Result::success)
        {
            End(result);
            return;
        }

        do
        {
            auto entry = infra::Head(infra::DiscardHead(window.Bytes(), (position->entry % entriesPerSector) * entryBytes), entryBytes);
            auto visit = VisitEntry(entry);

            if (visit == Visit::end)
            {
                End(fs::Result::success);
                return;
            }

            ++position->entry;

            if (visit == Visit::found)
            {
                Found(entry);
                return;
            }
        } while (position->entry % entriesPerSector != 0);

        Advance();
    }

    FatScan::Visit FatScan::VisitEntry(infra::ConstByteRange entry)
    {
        if (entry[0] == endOfDirectory)
            return Visit::end;

        if (entry[0] != freeEntry && entry[11] == longNameAttributes)
            AddLongEntry(entry);
        else if (entry[0] == freeEntry || entry[0] == '.' || (entry[11] & volumeLabel) != 0)
            ForgetLongName();
        else
            return Visit::found;

        return Visit::skipped;
    }

    void FatScan::AddLongEntry(infra::ConstByteRange entry)
    {
        uint8_t ordinal = entry[0] & ordinalMask;

        if ((entry[0] & lastLongEntry) != 0)
        {
            longNext = ordinal <= maxOrdinal ? ordinal : 0;
            longChecksum = entry[13];
            longUnits = static_cast<uint16_t>(longNext * unitsPerLongEntry);
        }

        if (ordinal == 0 || ordinal != longNext || entry[13] != longChecksum)
        {
            ForgetLongName();
            return;
        }

        for (std::size_t index = 0; index != unitsPerLongEntry; ++index)
            longName[(ordinal - 1) * unitsPerLongEntry + index] = static_cast<char16_t>(fs::LoadLittle16(entry, unitOffsets[index]));

        --longNext;
        longComplete = longNext == 0;
    }

    void FatScan::ForgetLongName()
    {
        longNext = 0;
        longComplete = false;
    }

    void FatScan::Found(infra::ConstByteRange entry)
    {
        FormatShortName(entry);

        if (!longComplete || ShortNameChecksum(entry) != longChecksum || !UseLongName())
            name.assign(shortName);

        uint32_t high = layout.kind == FatKind::fat32 ? fs::LoadLittle16(entry, 20) : 0;
        onDone(fs::Result::success, FatEntry{ name, shortName, (high << 16) | fs::LoadLittle16(entry, 26), fs::LoadLittle32(entry, 28), (entry[11] & directoryAttribute) != 0 });
    }

    bool FatScan::UseLongName()
    {
        name.clear();

        for (std::size_t index = 0; index != longUnits && longName[index] != 0; ++index)
        {
            char32_t codePoint = longName[index];

            if (fs::IsHighSurrogate(longName[index]) && index + 1 != longUnits && fs::IsLowSurrogate(longName[index + 1]))
            {
                codePoint = fs::CombineSurrogates(longName[index], longName[index + 1]);
                ++index;
            }
            else if (fs::IsHighSurrogate(longName[index]) || fs::IsLowSurrogate(longName[index]))
                return false;

            if (!Append(name, codePoint))
                return false;
        }

        return !name.empty();
    }

    void FatScan::FormatShortName(infra::ConstByteRange entry)
    {
        std::array<uint8_t, 8> base{};
        infra::Copy(infra::Head(entry, base.size()), infra::MakeRange(base));

        if (base[0] == escapedFreeByte)
            base[0] = freeEntry;

        shortName.clear();
        AppendShortPart(shortName, infra::MakeRange(base), (entry[12] & lowerBase) != 0);

        if (entry[8] != ' ')
        {
            shortName.push_back('.');
            AppendShortPart(shortName, infra::Head(infra::DiscardHead(entry, base.size()), 3), (entry[12] & lowerExtension) != 0);
        }
    }

    void FatScan::Advance()
    {
        ++position->sector;

        if (position->cluster == 0 && position->sector == layout.rootSectors)
        {
            position->entry = maxEntries;
            End(fs::Result::success);
        }
        else if (position->cluster != 0 && position->sector == (uint32_t{ 1 } << layout.clusterShift))
            chain.Next(position->cluster, [this](fs::Result result, uint32_t next)
                {
                    NextCluster(result, next);
                });
        else
            Load();
    }

    void FatScan::NextCluster(fs::Result result, uint32_t next)
    {
        if (result == fs::Result::success && next != 0)
        {
            position->cluster = next;
            position->sector = 0;
            Load();
        }
        else
        {
            if (result == fs::Result::success)
                position->entry = maxEntries;

            End(result);
        }
    }

    void FatScan::End(fs::Result result)
    {
        onDone(result, std::nullopt);
    }
}
