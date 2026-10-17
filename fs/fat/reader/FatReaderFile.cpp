#include "fs/fat/reader/FatReaderFile.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include <algorithm>

namespace fs
{
    FatReaderFile::FatReaderFile(FatReaderVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatReaderFile::Open(const FatScannedEntry& entry)
    {
        generation = volume.Generation();
        firstCluster = entry.entry.firstCluster;
        size = entry.entry.size;
        position = 0;
        cluster = firstCluster;
        clusterIndex = 0;
    }

    uint32_t FatReaderFile::Size() const
    {
        return size;
    }

    uint32_t FatReaderFile::Position() const
    {
        return position;
    }

    void FatReaderFile::Seek(uint32_t newPosition)
    {
        position = std::min(newPosition, size);
    }

    void FatReaderFile::Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
    {
        if (onRead)
            volume.Defer(onDone, Result::busy);
        else
        {
            this->buffer = buffer;
            bytesRead = 0;
            onRead = onDone;
            self = Share(*this);
            claimer.Claim([this]
                {
                    Granted();
                });
        }
    }

    void FatReaderFile::Write(infra::ConstByteRange, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatReaderFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatReaderFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatReaderFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    void FatReaderFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    void FatReaderFile::UnReferenced()
    {
        Release();
    }

    void FatReaderFile::Granted()
    {
        if (volume.Available(generation))
            Step();
        else
            Finish(volume.Unavailable());
    }

    void FatReaderFile::Step()
    {
        auto index = position / ClusterBytes();

        if (clusterIndex > index)
        {
            cluster = firstCluster;
            clusterIndex = 0;
        }

        if (Remaining() == 0)
            Done(Result::success);
        else if (index >= volume.Geometry().clusterCount)
            Done(Result::corrupted);
        else if (clusterIndex != index)
            volume.Table().Next(cluster, [this](Result result, std::optional<uint32_t> next)
                {
                    Located(result, next);
                });
        else
            Transfer();
    }

    void FatReaderFile::Located(Result result, std::optional<uint32_t> next)
    {
        if (result == Result::success && !next.has_value())
            result = Result::corrupted;

        if (result != Result::success)
            Done(result);
        else
        {
            cluster = *next;
            ++clusterIndex;
            Step();
        }
    }

    void FatReaderFile::Transfer()
    {
        auto sectorSize = volume.Sectors().SectorSize();
        auto sectorInCluster = position % ClusterBytes() / sectorSize;
        auto sector = volume.Geometry().SectorOfCluster(cluster) + sectorInCluster;

        if (position % sectorSize != 0 || Remaining() < sectorSize || !volume.Alignment().Accepts(infra::DiscardHead(buffer, bytesRead)))
            TransferPart(sector);
        else
            TransferSectors(sector, static_cast<uint32_t>(std::min<std::size_t>(Remaining() / sectorSize, volume.Geometry().sectorsPerCluster - sectorInCluster)));
    }

    void FatReaderFile::TransferPart(uint32_t sector)
    {
        volume.Sectors().Read(sector, [this](Result result, infra::ConstByteRange contents)
            {
                auto offset = position % contents.size();
                auto bytes = std::min<std::size_t>(contents.size() - offset, Remaining());

                if (result == Result::success)
                    infra::Copy(infra::Head(infra::DiscardHead(contents, offset), bytes), infra::Head(infra::DiscardHead(buffer, bytesRead), bytes));

                Transferred(result, bytes);
            });
    }

    void FatReaderFile::TransferSectors(uint32_t sector, uint32_t sectors)
    {
        transferBytes = std::size_t{ sectors } * volume.Sectors().SectorSize();
        volume.Device().ReadBlocks(infra::Head(infra::DiscardHead(buffer, bytesRead), transferBytes), sector, [this](hal::BlockDevice::Result result)
            {
                Transferred(ToResult(result), transferBytes);
            });
    }

    void FatReaderFile::Transferred(Result result, std::size_t bytes)
    {
        if (result != Result::success)
            Done(result);
        else
        {
            position += static_cast<uint32_t>(bytes);
            bytesRead += bytes;
            Step();
        }
    }

    void FatReaderFile::Done(Result result)
    {
        volume.Report(result);
        Finish(result);
    }

    void FatReaderFile::Finish(Result result)
    {
        claimer.Release();
        onRead(result, bytesRead);
        self = nullptr;
    }

    std::size_t FatReaderFile::Remaining() const
    {
        return std::min<std::size_t>(buffer.size() - bytesRead, size - position);
    }

    uint32_t FatReaderFile::ClusterBytes() const
    {
        return volume.Geometry().sectorsPerCluster * volume.Sectors().SectorSize();
    }

    Result FatReaderFile::Writable() const
    {
        return volume.Available(generation) ? Result::readOnly : volume.Unavailable();
    }

    Result FatReaderFile::Settled() const
    {
        return volume.Available(generation) ? Result::success : volume.Unavailable();
    }
}
