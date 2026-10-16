#include "spike/fat_reader/FatReaderFile.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include <algorithm>

namespace spike
{
    FatReaderFile::FatReaderFile(FatReaderVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    void FatReaderFile::Open(const FatEntry& entry)
    {
        generation = volume.Generation();
        firstCluster = entry.firstCluster;
        size = entry.size;
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

    void FatReaderFile::Read(infra::ByteRange buffer, const infra::Function<void(fs::Result result, std::size_t bytesRead)>& onDone)
    {
        if (onRead)
            volume.Defer(onDone, fs::Result::busy);
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

    void FatReaderFile::Write(infra::ConstByteRange, const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatReaderFile::Truncate(const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatReaderFile::ExpectSize(uint32_t, const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Writable());
    }

    void FatReaderFile::Sync(const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Settled());
    }

    void FatReaderFile::Close(const infra::Function<void(fs::Result result)>& onDone)
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
            Done(fs::Result::notMounted);
    }

    void FatReaderFile::Step()
    {
        const auto& layout = volume.Layout();
        uint32_t index = position >> (layout.clusterShift + 9);

        if (Remaining() == 0)
            Done(fs::Result::success);
        else if (!IsCluster(layout, cluster))
            Done(fs::Result::corrupted);
        else if (clusterIndex > index)
        {
            cluster = firstCluster;
            clusterIndex = 0;
            Step();
        }
        else if (clusterIndex < index)
            volume.Chain().Next(cluster, [this](fs::Result result, uint32_t next)
                {
                    Located(result, next);
                });
        else
            Transfer();
    }

    void FatReaderFile::Located(fs::Result result, uint32_t next)
    {
        if (result != fs::Result::success)
            Done(result);
        else if (next == 0)
            Done(fs::Result::corrupted);
        else
        {
            cluster = next;
            ++clusterIndex;
            Step();
        }
    }

    void FatReaderFile::Transfer()
    {
        const auto& layout = volume.Layout();
        uint32_t sectorInCluster = (position / fatSectorBytes) & ((uint32_t{ 1 } << layout.clusterShift) - 1);
        uint32_t sector = layout.dataStart + ((cluster - 2) << layout.clusterShift) + sectorInCluster;

        if (position % fatSectorBytes != 0 || Remaining() < fatSectorBytes)
            volume.Window().Load(sector, [this](fs::Result result)
                {
                    Copied(result);
                });
        else
        {
            direct = std::min<std::size_t>(Remaining() / fatSectorBytes, (uint32_t{ 1 } << layout.clusterShift) - sectorInCluster) * fatSectorBytes;
            volume.Window().Device().ReadBlocks(infra::Head(infra::DiscardHead(buffer, bytesRead), direct), sector, [this](hal::BlockDevice::Result result)
                {
                    Advance(fs::ToResult(result), direct);
                });
        }
    }

    void FatReaderFile::Copied(fs::Result result)
    {
        auto offset = position % fatSectorBytes;
        auto count = std::min<std::size_t>(fatSectorBytes - offset, Remaining());

        if (result == fs::Result::success)
            infra::Copy(infra::Head(infra::DiscardHead(volume.Window().Bytes(), offset), count), infra::Head(infra::DiscardHead(buffer, bytesRead), count));

        Advance(result, count);
    }

    void FatReaderFile::Advance(fs::Result result, std::size_t bytes)
    {
        if (result != fs::Result::success)
            Done(result);
        else
        {
            position += static_cast<uint32_t>(bytes);
            bytesRead += bytes;
            Step();
        }
    }

    void FatReaderFile::Done(fs::Result result)
    {
        claimer.Release();
        onRead(result, bytesRead);
        self = nullptr;
    }

    std::size_t FatReaderFile::Remaining() const
    {
        return std::min<std::size_t>(buffer.size() - bytesRead, size - position);
    }

    fs::Result FatReaderFile::Writable() const
    {
        return volume.Available(generation) ? fs::Result::readOnly : fs::Result::notMounted;
    }

    fs::Result FatReaderFile::Settled() const
    {
        return volume.Available(generation) ? fs::Result::success : fs::Result::notMounted;
    }
}
