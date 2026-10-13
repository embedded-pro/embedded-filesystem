#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatTable.hpp"

namespace fs
{
    FatTableWriter::FatTableWriter(SectorCache& cache, const FatGeometry& geometry)
        : cache(cache)
        , geometry(geometry)
    {}

    FatTableWriter::~FatTableWriter() = default;

    void FatTableWriter::Write(uint32_t cluster, uint32_t value, const infra::Function<void(Result result)>& onDone)
    {
        this->cluster = cluster;
        this->value = value;
        this->onDone = onDone;
        straddles = LocateEntry(geometry.type, cluster, cache.SectorSize()).straddles;

        cache.Modify(SectorOf(cluster), tableRank, [this](Result result, infra::ByteRange contents)
            {
                if (result != Result::success)
                    this->onDone(result);
                else if (straddles)
                {
                    WriteStraddlingPart(this->cluster, this->value, contents, false);
                    WriteSecondPart();
                }
                else
                {
                    WriteEntry(geometry.type, this->cluster, this->value, contents, infra::ByteRange());
                    this->onDone(result);
                }
            });
    }

    uint32_t FatTableWriter::SectorOf(uint32_t cluster) const
    {
        return geometry.ActiveFatSector() + LocateEntry(geometry.type, cluster, cache.SectorSize()).sector;
    }

    void FatTableWriter::WriteSecondPart()
    {
        cache.Modify(SectorOf(cluster) + 1, tableRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                    WriteStraddlingPart(cluster, value, contents, true);

                onDone(result);
            });
    }
}
