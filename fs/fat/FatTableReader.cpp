#include "fs/fat/FatTableReader.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatTableReader::FatTableReader(SectorCache& cache, const FatGeometry& geometry)
        : cache(cache)
        , geometry(geometry)
    {}

    FatTableReader::~FatTableReader() = default;

    void FatTableReader::Read(uint32_t cluster, const infra::Function<void(Result result, uint32_t value)>& onDone)
    {
        really_assert(geometry.IsCluster(cluster));

        this->cluster = cluster;
        onRead = onDone;
        auto location = LocateEntry(geometry.type, cluster, cache.SectorSize());
        sector = geometry.ActiveFatSector() + location.sector;
        offset = location.offset;

        cache.Read(sector, [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatTableReader::Next(uint32_t cluster, const infra::Function<void(Result result, std::optional<uint32_t> next)>& onDone)
    {
        onNext = onDone;
        Read(cluster, [this](Result result, uint32_t value)
            {
                if (result != Result::success)
                    onNext(result, std::nullopt);
                else if (IsEndOfChain(geometry.type, value))
                    onNext(Result::success, std::nullopt);
                else if (!geometry.IsCluster(value))
                    onNext(Result::corrupted, std::nullopt);
                else
                    onNext(Result::success, value);
            });
    }

    void FatTableReader::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
            Complete(result, 0);
        else if (offset + 1 != contents.size() || geometry.type != FatType::fat12)
            Complete(Result::success, ReadEntry(geometry.type, cluster, contents, infra::ConstByteRange()));
        else
        {
            lowByte = contents[offset];
            cache.Read(sector + 1, [this](Result result, infra::ConstByteRange contents)
                {
                    OnNextSector(result, contents);
                });
        }
    }

    void FatTableReader::OnNextSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
            Complete(result, 0);
        else
            Complete(Result::success, DecodeFat12(cluster, static_cast<uint16_t>(lowByte | (contents[0] << 8))));
    }

    void FatTableReader::Complete(Result result, uint32_t value)
    {
        onRead(result, value);
    }
}
