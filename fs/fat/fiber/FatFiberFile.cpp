#include "fs/fat/fiber/FatFiberFile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>

namespace fs
{
    FatFiberFile::FatFiberFile(FatFiberVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatFiberFile::Open(const FatScannedEntry& entry)
    {
        generation = volume.Generation();
        firstCluster = entry.entry.firstCluster;
        size = entry.entry.size;
        position = 0;
        cluster = firstCluster;
        clusterIndex = 0;
    }

    uint32_t FatFiberFile::Size() const
    {
        return size;
    }

    uint32_t FatFiberFile::Position() const
    {
        return position;
    }

    void FatFiberFile::Seek(uint32_t newPosition)
    {
        position = std::min(newPosition, size);
    }

    void FatFiberFile::Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
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
                    volume.Fiber().Run(claimer, [this]
                        {
                            RunRead();
                        });
                });
        }
    }

    void FatFiberFile::Write(infra::ConstByteRange, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatFiberFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatFiberFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatFiberFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    void FatFiberFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    void FatFiberFile::UnReferenced()
    {
        Release();
    }

    void FatFiberFile::RunRead()
    {
        if (!volume.Available(generation))
            readResult = volume.Unavailable();
        else
        {
            readResult = ReadAll();
            volume.Report(readResult);
        }

        infra::EventDispatcher::Instance().Schedule([this]
            {
                ReadCompleted();
            });
    }

    Result FatFiberFile::ReadAll()
    {
        while (Remaining() != 0)
        {
            auto index = position / ClusterBytes();

            if (clusterIndex > index)
            {
                cluster = firstCluster;
                clusterIndex = 0;
            }

            if (index >= volume.Geometry().clusterCount)
                return Result::corrupted;

            if (clusterIndex != index)
            {
                auto result = volume.NextCluster(cluster);

                if (result == Result::success && !volume.NextClusterFound())
                    result = Result::corrupted;

                if (result != Result::success)
                    return result;

                cluster = *volume.NextClusterFound();
                ++clusterIndex;
                continue;
            }

            auto sectorSize = volume.Window().SectorSize();
            auto sectorInCluster = position % ClusterBytes() / sectorSize;
            auto sector = volume.Geometry().SectorOfCluster(cluster) + sectorInCluster;
            std::size_t bytes = 0;

            if (position % sectorSize != 0 || Remaining() < sectorSize)
            {
                if (auto result = volume.Window().Read(sector); result != Result::success)
                    return result;

                auto offset = position % sectorSize;
                bytes = std::min<std::size_t>(sectorSize - offset, Remaining());
                infra::Copy(infra::Head(infra::DiscardHead(volume.Window().Contents(), offset), bytes), infra::Head(infra::DiscardHead(buffer, bytesRead), bytes));
            }
            else
            {
                bytes = std::min<std::size_t>(Remaining() / sectorSize, volume.Geometry().sectorsPerCluster - sectorInCluster) * sectorSize;

                if (auto result = volume.Fiber().Read(infra::Head(infra::DiscardHead(buffer, bytesRead), bytes), sector); result != Result::success)
                    return result;
            }

            position += static_cast<uint32_t>(bytes);
            bytesRead += bytes;
        }

        return Result::success;
    }

    void FatFiberFile::ReadCompleted()
    {
        onRead(readResult, bytesRead);
        self = nullptr;
    }

    std::size_t FatFiberFile::Remaining() const
    {
        return std::min<std::size_t>(buffer.size() - bytesRead, size - position);
    }

    uint32_t FatFiberFile::ClusterBytes() const
    {
        return volume.Geometry().sectorsPerCluster * volume.Window().SectorSize();
    }

    Result FatFiberFile::Writable() const
    {
        return volume.Available(generation) ? Result::readOnly : volume.Unavailable();
    }

    Result FatFiberFile::Settled() const
    {
        return volume.Available(generation) ? Result::success : volume.Unavailable();
    }
}
