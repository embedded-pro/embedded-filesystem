#include "fs/fat/FatWrite.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatVolume.hpp"
#include <algorithm>
#include <limits>

namespace fs
{
    FatWrite::FatWrite(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , file(file)
        , extents(extents)
        , data(data)
        , onDone(onDone)
        , resume([this](Result result)
              {
                  Resume(result);
              })
    {}

    FatWrite::~FatWrite() = default;

    void FatWrite::Start()
    {
        if (file.mode == OpenMode::append)
            file.position = file.size;

        auto room = std::numeric_limits<uint32_t>::max() - file.position;

        if (data.size() > room)
        {
            data = infra::Head(data, room);
            endResult = Result::noSpace;
        }

        if (data.empty())
            Next();
        else
            volume.MarkDirty(Then(Step::dirtied));
    }

    void FatWrite::Next()
    {
        if (data.empty())
        {
            onDone(endResult);
            return;
        }

        auto index = file.position / ClusterBytes();

        if (index >= file.clusters)
        {
            Grow();
            return;
        }

        auto limit = static_cast<uint32_t>(std::min<uint64_t>(uint64_t{ file.position } + data.size(), uint64_t{ file.clusters } * ClusterBytes()) - 1) / ClusterBytes();
        Locate(index, limit, Step::located);
    }

    void FatWrite::Grow()
    {
        if (file.clusters == 0)
            Allocate(LogStart(), std::nullopt);
        else
            Locate(file.clusters - 1, file.clusters - 1, Step::lastLocated);
    }

    void FatWrite::Allocate(std::optional<uint32_t> start, std::optional<uint32_t> last)
    {
        auto onGrown = [this](Result result, uint32_t first, uint32_t allocated)
        {
            OnGrown(result, first, allocated);
        };

        if (start.has_value())
            volume.Allocator().GrowAt(*start, Needed(), onGrown);
        else
            volume.Allocator().Grow(last, Needed(), file.reservation, file.committedFirstCluster != 0 ? FatChain::referenced : FatChain::unreferenced, onGrown);
    }

    void FatWrite::OnGrown(Result result, uint32_t first, uint32_t allocated)
    {
        if (result != Result::success && (result != Result::noSpace || allocated == 0))
        {
            onDone(result);
            return;
        }

        if (file.clusters == 0)
            file.firstCluster = first;

        extents.Add(FatExtent{ file.clusters, first, volume.Allocator().Contiguous() });
        file.clusters += allocated;

        if (result == Result::noSpace)
        {
            data = infra::Head(data, uint64_t{ file.clusters } * ClusterBytes() - file.position);
            endResult = result;
        }

        Next();
    }

    void FatWrite::Locate(uint32_t first, uint32_t last, Step next)
    {
        step = next;
        volume.Locator().Locate(extents, first, last, [this](Result result, const FatExtent& run)
            {
                Located(result, run);
            });
    }

    void FatWrite::Located(Result result, const FatExtent& run)
    {
        if (result != Result::success)
            onDone(result);
        else if (step == Step::lastLocated)
            Allocate(std::nullopt, run.ClusterAt(file.clusters - 1));
        else
            Transfer(run);
    }

    void FatWrite::Transfer(const FatExtent& run)
    {
        auto index = file.position / ClusterBytes();
        auto sectorInCluster = file.position % ClusterBytes() / SectorSize();
        auto sector = volume.Geometry().SectorOfCluster(run.ClusterAt(index)) + sectorInCluster;
        auto offset = file.position % SectorSize();

        if (offset != 0 || data.size() < SectorSize())
        {
            TransferSector(sector, offset, std::min<uint32_t>(static_cast<uint32_t>(data.size()), SectorSize() - offset));
            return;
        }

        auto sectorsInRun = uint64_t{ run.End() - index } * volume.Geometry().sectorsPerCluster - sectorInCluster;
        auto sectors = static_cast<uint32_t>(std::min<uint64_t>(data.size() / SectorSize(), sectorsInRun));

        if (sectors > volume.CacheSectors() - 2 && volume.Transfers().Accepts(data))
            TransferSectors(sector, sectors);
        else
            TransferSector(sector, 0, SectorSize());
    }

    void FatWrite::TransferSectors(uint32_t sector, uint32_t sectors)
    {
        partBytes = sectors * SectorSize();
        volume.Transfers().Write(sector, infra::Head(data, partBytes), dataRank, Then(Step::transferred));
    }

    void FatWrite::TransferSector(uint32_t sector, uint32_t offset, uint32_t bytes)
    {
        partOffset = offset;
        partBytes = bytes;

        auto fill = [this](Result result, infra::ByteRange contents)
        {
            if (result == Result::success)
                infra::Copy(infra::Head(data, partBytes), infra::Head(infra::DiscardHead(contents, partOffset), partBytes));

            Transferred(result);
        };

        if (file.position - offset >= file.size || (offset == 0 && bytes == SectorSize()))
            volume.Cache().Overwrite(sector, dataRank, fill);
        else
            volume.Cache().Modify(sector, dataRank, fill);
    }

    void FatWrite::Transferred(Result result)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        file.position += partBytes;
        file.size = std::max(file.size, file.position);
        file.written = true;
        data = infra::DiscardHead(data, partBytes);
        Next();
    }

    const infra::Function<void(Result result)>& FatWrite::Then(Step next)
    {
        step = next;
        return resume;
    }

    void FatWrite::Resume(Result result)
    {
        if (step == Step::transferred)
            Transferred(result);
        else if (result != Result::success)
            onDone(result);
        else
            Next();
    }

    uint32_t FatWrite::SectorSize() const
    {
        return volume.Cache().SectorSize();
    }

    uint32_t FatWrite::ClusterBytes() const
    {
        return volume.Geometry().sectorsPerCluster * SectorSize();
    }

    uint32_t FatWrite::Needed() const
    {
        return static_cast<uint32_t>((uint64_t{ file.position } + data.size() + ClusterBytes() - 1) / ClusterBytes() - file.clusters);
    }

    std::optional<uint32_t> FatWrite::LogStart() const
    {
        const auto& units = volume.State().allocationUnits;

        if (file.mode != OpenMode::append || file.reservation.count != 0 || !units.has_value())
            return std::nullopt;

        return volume.FreeSpace().Runs().WholeUnit(*units, volume.Allocator().Cursor(), volume.Reservations());
    }
}
