#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatShortName.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t sectorBytes = 512;
        constexpr uint32_t entriesPerSector = sectorBytes / directoryEntryBytes;
        constexpr uint16_t firstOf1980 = (1 << 5) | 1;

        ShortName ShortNameOf(const char (&name)[12])
        {
            ShortName shortName;
            std::copy_n(name, shortName.bytes.size(), shortName.bytes.begin());
            return shortName;
        }

        ShortEntry EntryOf(const char (&name)[12], uint8_t attributes)
        {
            ShortEntry entry;
            entry.name = ShortNameOf(name);
            entry.attributes = attributes;
            entry.createDate = firstOf1980;
            entry.accessDate = firstOf1980;
            entry.writeDate = firstOf1980;
            return entry;
        }
    }

    FatImageBuilder::FatImageBuilder(BlockImage& device)
        : device(device)
    {
        auto first = ReadSector(0);
        uint32_t start = 0;
        uint32_t sectors = device.Blocks();
        auto parameters = ParseBootSector(infra::MakeRange(first));

        if (!parameters)
        {
            auto partition = FirstFatPartition(infra::MakeRange(first));
            really_assert(partition.has_value());
            start = partition->firstSector;
            sectors = partition->sectors;
            parameters = ParseBootSector(infra::MakeRange(ReadSector(start)));
        }

        really_assert(parameters.has_value());
        auto result = GeometryOf(*parameters, start, sectors, sectorBytes);
        really_assert(result.result == Result::success);
        geometry = result.geometry;
    }

    const FatGeometry& FatImageBuilder::Geometry() const
    {
        return geometry;
    }

    uint32_t FatImageBuilder::RootCluster() const
    {
        return geometry.type == FatType::fat32 ? geometry.rootCluster : 0;
    }

    uint32_t FatImageBuilder::AddFile(uint32_t parent, infra::BoundedConstString longName, const char (&shortName)[12], infra::ConstByteRange contents, bool fragmented)
    {
        auto entry = EntryOf(shortName, FatAttributes::archive);
        entry.size = static_cast<uint32_t>(contents.size());

        if (!contents.empty())
        {
            entry.firstCluster = AllocateChain(static_cast<uint32_t>((contents.size() + ClusterBytes() - 1) / ClusterBytes()), fragmented);
            WriteData(entry.firstCluster, contents);
        }

        AddEntries(parent, longName, entry);
        return entry.firstCluster;
    }

    uint32_t FatImageBuilder::AddDirectory(uint32_t parent, infra::BoundedConstString longName, const char (&shortName)[12])
    {
        auto entry = EntryOf(shortName, FatAttributes::directory);
        entry.firstCluster = AllocateChain(1);
        ZeroCluster(entry.firstCluster);

        auto dot = EntryOf(".          ", FatAttributes::directory);
        dot.firstCluster = entry.firstCluster;
        ShortEntry dotDot = dot;
        dotDot.name = ShortNameOf("..         ");
        dotDot.firstCluster = parent == RootCluster() ? 0 : parent;

        std::array<uint8_t, directoryEntryBytes> raw;
        EncodeShortEntry(dot, infra::MakeRange(raw));
        WriteRawEntry(FatEntryPosition{ entry.firstCluster, 0 }, infra::MakeRange(raw));
        EncodeShortEntry(dotDot, infra::MakeRange(raw));
        WriteRawEntry(FatEntryPosition{ entry.firstCluster, 1 }, infra::MakeRange(raw));

        AddEntries(parent, longName, entry);
        return entry.firstCluster;
    }

    FatEntryPosition FatImageBuilder::AddEntries(uint32_t parent, infra::BoundedConstString longName, const ShortEntry& entry)
    {
        auto longEntries = longName.empty() ? 0 : LongEntriesFor(longName);
        auto position = EndOfDirectory(parent, longEntries + 1u);
        auto checksum = LongNameChecksum(entry.name.bytes);
        std::array<uint8_t, directoryEntryBytes> raw;

        for (auto ordinal = longEntries; ordinal != 0; --ordinal)
        {
            EncodeLongEntry(longName, static_cast<uint8_t>(ordinal), ordinal == longEntries, checksum, infra::MakeRange(raw));
            WriteRawEntry(position, infra::MakeRange(raw));
            ++position.index;
        }

        EncodeShortEntry(entry, infra::MakeRange(raw));
        WriteRawEntry(position, infra::MakeRange(raw));
        return position;
    }

    uint32_t FatImageBuilder::AllocateChain(uint32_t clusters, bool fragmented)
    {
        really_assert(clusters != 0);

        auto first = FreeCluster(2);
        auto previous = first;

        for (uint32_t index = 1; index != clusters; ++index)
        {
            auto next = FreeCluster(previous + (fragmented ? 2 : 1));
            SetFatEntry(previous, next);
            previous = next;
        }

        SetFatEntry(previous, EndOfChain(geometry.type));
        return first;
    }

    uint32_t FatImageBuilder::FatEntry(uint32_t cluster) const
    {
        auto location = LocateEntry(geometry.type, cluster, sectorBytes);
        auto sector = ReadSector(geometry.ActiveFatSector() + location.sector);
        auto next = ReadSector(geometry.ActiveFatSector() + location.sector + 1);
        return ReadEntry(geometry.type, cluster, infra::MakeRange(sector), infra::MakeRange(next));
    }

    void FatImageBuilder::SetFatEntry(uint32_t cluster, uint32_t value)
    {
        auto location = LocateEntry(geometry.type, cluster, sectorBytes);

        for (uint8_t copy = 0; copy != geometry.fatCopies; ++copy)
        {
            auto first = geometry.firstFatSector + copy * geometry.sectorsPerFat + location.sector;
            auto sector = ReadSector(first);
            auto next = ReadSector(first + 1);
            WriteEntry(geometry.type, cluster, value, infra::MakeRange(sector), infra::MakeRange(next));
            WriteSector(first, sector);

            if (location.straddles)
                WriteSector(first + 1, next);
        }
    }

    void FatImageBuilder::WriteRawEntry(FatEntryPosition position, infra::ConstByteRange entry)
    {
        auto sector = SectorOf(position);
        auto contents = ReadSector(sector);
        infra::Copy(entry, infra::Head(infra::DiscardHead(infra::MakeRange(contents), (position.index % entriesPerSector) * directoryEntryBytes), directoryEntryBytes));
        WriteSector(sector, contents);
    }

    FatImageBuilder::Sector FatImageBuilder::ReadSector(uint32_t sector) const
    {
        Sector contents;
        device.ReadBlock(sector, infra::MakeRange(contents));
        return contents;
    }

    uint32_t FatImageBuilder::SectorOf(FatEntryPosition position) const
    {
        auto first = position.cluster == 0 ? geometry.rootSector : geometry.SectorOfCluster(position.cluster);
        return first + position.index / entriesPerSector;
    }

    uint32_t FatImageBuilder::ClusterBytes() const
    {
        return geometry.sectorsPerCluster * sectorBytes;
    }

    void FatImageBuilder::WriteSector(uint32_t sector, const Sector& contents)
    {
        device.WriteBlock(sector, infra::MakeRange(contents));
    }

    void FatImageBuilder::WriteData(uint32_t firstCluster, infra::ConstByteRange contents)
    {
        auto cluster = firstCluster;

        while (!contents.empty())
        {
            for (uint32_t sector = 0; sector != geometry.sectorsPerCluster && !contents.empty(); ++sector)
            {
                Sector data{};
                auto part = infra::Head(contents, sectorBytes);
                infra::Copy(part, infra::Head(infra::MakeRange(data), part.size()));
                WriteSector(geometry.SectorOfCluster(cluster) + sector, data);
                contents = infra::DiscardHead(contents, part.size());
            }

            cluster = FatEntry(cluster);
        }
    }

    void FatImageBuilder::ZeroCluster(uint32_t cluster)
    {
        for (uint32_t sector = 0; sector != geometry.sectorsPerCluster; ++sector)
            WriteSector(geometry.SectorOfCluster(cluster) + sector, Sector{});
    }

    uint32_t FatImageBuilder::FreeCluster(uint32_t from) const
    {
        auto cluster = from;

        while (FatEntry(cluster) != 0)
            ++cluster;

        really_assert(geometry.IsCluster(cluster));
        return cluster;
    }

    FatEntryPosition FatImageBuilder::EndOfDirectory(uint32_t directory, uint32_t slots)
    {
        auto cluster = directory == 0 && geometry.type == FatType::fat32 ? geometry.rootCluster : directory;
        auto last = cluster == 0 ? 0 : LastCluster(cluster);
        uint32_t used = 0;

        for (uint32_t index = 0; index != EntriesPerCluster(cluster); ++index)
            if (ReadSector(SectorOf(FatEntryPosition{ last, index }))[(index % entriesPerSector) * directoryEntryBytes] != 0)
                used = index + 1;

        if (used + slots <= EntriesPerCluster(cluster))
            return FatEntryPosition{ last, used };

        really_assert(cluster != 0);
        auto next = AllocateChain(1);
        ZeroCluster(next);
        SetFatEntry(last, next);
        return FatEntryPosition{ next, 0 };
    }

    uint32_t FatImageBuilder::EntriesPerCluster(uint32_t directory) const
    {
        if (directory == 0)
            return geometry.rootSectors * entriesPerSector;

        return geometry.sectorsPerCluster * entriesPerSector;
    }

    uint32_t FatImageBuilder::LastCluster(uint32_t first) const
    {
        auto cluster = first;

        while (!IsEndOfChain(geometry.type, FatEntry(cluster)))
            cluster = FatEntry(cluster);

        return cluster;
    }
}
