#include "fs/fat/FatRead.hpp"
#include "fs/fat/FatVolume.hpp"
#include <algorithm>

namespace fs
{
    FatRead::FatRead(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
        : volume(volume)
        , file(file)
        , extents(extents)
        , buffer(buffer)
        , onDone(onDone)
    {}

    FatRead::~FatRead() = default;

    void FatRead::Start()
    {
        if (Remaining() == 0)
            onDone(Result::success, bytesRead);
        else
            Locate();
    }

    void FatRead::Locate()
    {
        auto index = file.position / ClusterBytes();
        auto limit = (file.position + Remaining() - 1) / ClusterBytes();

        volume.Locator().Locate(extents, index, limit, [this](Result result, const FatExtent& run)
            {
                if (result != Result::success)
                    onDone(result, bytesRead);
                else
                    Transfer(run);
            });
    }

    void FatRead::Transfer(const FatExtent& run)
    {
        auto index = file.position / ClusterBytes();
        auto sectorInCluster = file.position % ClusterBytes() / SectorSize();
        auto sector = volume.Geometry().SectorOfCluster(run.ClusterAt(index)) + sectorInCluster;
        auto offset = file.position % SectorSize();

        if (offset != 0 || Remaining() < SectorSize())
            TransferPart(sector, offset, std::min(Remaining(), SectorSize() - offset));
        else
        {
            auto sectorsInRun = uint64_t{ run.End() - index } * volume.Geometry().sectorsPerCluster - sectorInCluster;
            TransferSectors(sector, static_cast<uint32_t>(std::min<uint64_t>(Remaining() / SectorSize(), sectorsInRun)));
        }
    }

    void FatRead::TransferSectors(uint32_t sector, uint32_t sectors)
    {
        auto bytes = sectors * SectorSize();

        volume.Transfers().Read(sector, infra::Head(buffer, bytes), [this, bytes](Result result)
            {
                Transferred(result, bytes);
            });
    }

    void FatRead::TransferPart(uint32_t sector, uint32_t offset, uint32_t bytes)
    {
        partOffset = offset;
        partBytes = bytes;
        volume.Cache().Read(sector, [this](Result result, infra::ConstByteRange contents)
            {
                if (result == Result::success)
                    infra::Copy(infra::Head(infra::DiscardHead(contents, partOffset), partBytes), infra::Head(buffer, partBytes));

                Transferred(result, partBytes);
            });
    }

    void FatRead::Transferred(Result result, uint32_t bytes)
    {
        if (result != Result::success)
        {
            onDone(result, bytesRead);
            return;
        }

        file.position += bytes;
        bytesRead += bytes;
        buffer = infra::DiscardHead(buffer, bytes);
        Start();
    }

    uint32_t FatRead::Remaining() const
    {
        return static_cast<uint32_t>(std::min<std::size_t>(buffer.size(), file.size - file.position));
    }

    uint32_t FatRead::SectorSize() const
    {
        return volume.Cache().SectorSize();
    }

    uint32_t FatRead::ClusterBytes() const
    {
        return volume.Geometry().sectorsPerCluster * SectorSize();
    }
}
