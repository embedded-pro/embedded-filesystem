#include "fs/fat/FatTableReader.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatTableReader::FatTableReader(SectorSource& sectors, const FatGeometry& geometry)
        : sectors(sectors)
        , geometry(geometry)
    {}

    FatTableReader::~FatTableReader() = default;

    void FatTableReader::Read(uint32_t cluster, const infra::Function<void(Result result, uint32_t value)>& onDone)
    {
        onRead = onDone;
        Locate(cluster);
    }

    void FatTableReader::Next(uint32_t cluster, const infra::Function<void(Result result, std::optional<uint32_t> next)>& onDone)
    {
        onNext = onDone;
        Locate(cluster);
    }

    void FatTableReader::Locate(uint32_t cluster)
    {
        really_assert(geometry.IsCluster(cluster));

        this->cluster = cluster;
        auto location = LocateEntry(geometry.type, cluster, sectors.SectorSize());
        sector = geometry.ActiveFatSector() + location.sector;
        offset = location.offset;
        nextSector = false;
        ReadSector();
    }

    void FatTableReader::ReadSector()
    {
        sectors.Read(nextSector ? sector + 1 : sector, [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatTableReader::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
            Complete(result, 0);
        else if (nextSector)
            Complete(Result::success, DecodeFat12(cluster, static_cast<uint16_t>(lowByte | (contents[0] << 8))));
        else if (offset + 1 != contents.size() || geometry.type != FatType::fat12)
            Complete(Result::success, ReadEntry(geometry.type, cluster, contents, infra::ConstByteRange()));
        else
        {
            lowByte = contents[offset];
            nextSector = true;
            ReadSector();
        }
    }

    void FatTableReader::Complete(Result result, uint32_t value)
    {
        if (onRead)
            onRead(result, value);
        else if (result != Result::success)
            onNext(result, std::nullopt);
        else if (IsEndOfChain(geometry.type, value))
            onNext(Result::success, std::nullopt);
        else if (!geometry.IsCluster(value))
            onNext(Result::corrupted, std::nullopt);
        else
            onNext(Result::success, value);
    }
}
