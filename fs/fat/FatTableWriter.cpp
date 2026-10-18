#include "fs/fat/FatTableWriter.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatTable.hpp"

namespace fs
{
    FatTableWriter::FatTableWriter(SectorStore& sectors, const FatGeometry& geometry)
        : sectors(sectors)
        , geometry(geometry)
    {}

    FatTableWriter::~FatTableWriter() = default;

    void FatTableWriter::Write(uint32_t cluster, uint32_t value, const infra::Function<void(Result result)>& onDone)
    {
        this->cluster = cluster;
        this->value = value;
        this->onDone = onDone;
        straddles = LocateEntry(geometry.type, cluster, sectors.SectorSize()).straddles;
        secondPart = false;
        Modify();
    }

    uint32_t FatTableWriter::SectorOf(uint32_t cluster) const
    {
        return geometry.ActiveFatSector() + LocateEntry(geometry.type, cluster, sectors.SectorSize()).sector;
    }

    void FatTableWriter::Modify()
    {
        sectors.Modify(secondPart ? SectorOf(cluster) + 1 : SectorOf(cluster), tableRank, [this](Result result, infra::ByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatTableWriter::OnSector(Result result, infra::ByteRange contents)
    {
        if (result != Result::success)
            onDone(result);
        else if (secondPart)
        {
            WriteStraddlingPart(cluster, value, contents, true);
            onDone(result);
        }
        else if (straddles)
        {
            WriteStraddlingPart(cluster, value, contents, false);
            secondPart = true;
            Modify();
        }
        else
        {
            WriteEntry(geometry.type, cluster, value, contents, infra::ByteRange());
            onDone(result);
        }
    }
}
