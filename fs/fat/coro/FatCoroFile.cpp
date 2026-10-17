#include "fs/fat/coro/FatCoroFile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>

namespace fs
{
    FatCoroFile::FatCoroFile(FatCoroVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatCoroFile::Open(const FatScannedEntry& entry)
    {
        generation = volume.Generation();
        firstCluster = entry.entry.firstCluster;
        size = entry.entry.size;
        position = 0;
        cluster = firstCluster;
        clusterIndex = 0;
    }

    uint32_t FatCoroFile::Size() const
    {
        return size;
    }

    uint32_t FatCoroFile::Position() const
    {
        return position;
    }

    void FatCoroFile::Seek(uint32_t newPosition)
    {
        position = std::min(newPosition, size);
    }

    void FatCoroFile::Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
    {
        if (onRead)
            volume.Defer(onDone, Result::busy);
        else
        {
            this->buffer = buffer;
            bytesRead = 0;
            onRead = onDone;
            self = Share(*this);
            RunRead(frames);
        }
    }

    void FatCoroFile::Write(infra::ConstByteRange, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatCoroFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatCoroFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatCoroFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    void FatCoroFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    std::size_t FatCoroFile::FramePeak() const
    {
        return frames.Peak();
    }

    void FatCoroFile::UnReferenced()
    {
        Release();
    }

    coro::Detached FatCoroFile::RunRead(coro::Frames&)
    {
        co_await coro::Claim{ claimer };

        if (!volume.Available(generation))
            readResult = volume.Unavailable();
        else
        {
            readResult = Result::success;

            while (Remaining() != 0)
            {
                auto index = position / ClusterBytes();

                if (clusterIndex > index)
                {
                    cluster = firstCluster;
                    clusterIndex = 0;
                }

                if (index >= volume.Geometry().clusterCount)
                {
                    readResult = Result::corrupted;
                    break;
                }

                if (clusterIndex != index)
                {
                    auto result = co_await volume.NextCluster(frames, cluster);

                    if (result == Result::success && !volume.NextClusterFound())
                        result = Result::corrupted;

                    if (result != Result::success)
                    {
                        readResult = result;
                        break;
                    }

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
                    if (auto result = co_await volume.Window().Read(sector); result != Result::success)
                    {
                        readResult = result;
                        break;
                    }

                    auto offset = position % sectorSize;
                    bytes = std::min<std::size_t>(sectorSize - offset, Remaining());
                    infra::Copy(infra::Head(infra::DiscardHead(volume.Window().Contents(), offset), bytes), infra::Head(infra::DiscardHead(buffer, bytesRead), bytes));
                }
                else
                {
                    bytes = std::min<std::size_t>(Remaining() / sectorSize, volume.Geometry().sectorsPerCluster - sectorInCluster) * sectorSize;

                    if (auto result = co_await coro::DeviceRead{ volume.Device(), infra::Head(infra::DiscardHead(buffer, bytesRead), bytes), sector }; result != Result::success)
                    {
                        readResult = result;
                        break;
                    }
                }

                position += static_cast<uint32_t>(bytes);
                bytesRead += bytes;
            }

            volume.Report(readResult);
        }

        claimer.Release();
        infra::EventDispatcher::Instance().Schedule([this]
            {
                ReadCompleted();
            });
    }

    void FatCoroFile::ReadCompleted()
    {
        onRead(readResult, bytesRead);
        self = nullptr;
    }

    std::size_t FatCoroFile::Remaining() const
    {
        return std::min<std::size_t>(buffer.size() - bytesRead, size - position);
    }

    uint32_t FatCoroFile::ClusterBytes() const
    {
        return volume.Geometry().sectorsPerCluster * volume.Window().SectorSize();
    }

    Result FatCoroFile::Writable() const
    {
        return volume.Available(generation) ? Result::readOnly : volume.Unavailable();
    }

    Result FatCoroFile::Settled() const
    {
        return volume.Available(generation) ? Result::success : volume.Unavailable();
    }
}
