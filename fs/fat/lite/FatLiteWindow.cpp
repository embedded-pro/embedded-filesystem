#include "fs/fat/lite/FatLiteWindow.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>

namespace fs
{
    FatLiteWindow::FatLiteWindow(FlushBarrier& barrier, infra::ByteRange buffer)
        : barrier(barrier)
        , buffer(buffer)
    {}

    FatLiteWindow::~FatLiteWindow() = default;

    uint32_t FatLiteWindow::SectorSize() const
    {
        return static_cast<uint32_t>(buffer.size());
    }

    void FatLiteWindow::Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone)
    {
        onRead = onDone;
        Start(Access::read, sector, 0);
    }

    void FatLiteWindow::Modify(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone)
    {
        onModify = onDone;
        Start(Access::modify, sector, rank);
    }

    void FatLiteWindow::Overwrite(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone)
    {
        onModify = onDone;
        Start(Access::overwrite, sector, rank);
    }

    void FatLiteWindow::WriteBack(const infra::Function<void(Result result)>& onDone)
    {
        onWrittenBack = onDone;

        if (dirty)
        {
            copy = 0;
            WriteCopy();
        }
        else
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    onWrittenBack(Result::success);
                });
    }

    void FatLiteWindow::Flush(const infra::Function<void(Result result)>& onDone)
    {
        onFlushed = onDone;
        WriteBack([this](Result result)
            {
                if (result != Result::success)
                    onFlushed(result);
                else
                    barrier.Flush([this](hal::BlockDevice::Result result)
                        {
                            onFlushed(ToResult(result));
                        });
            });
    }

    void FatLiteWindow::SetMirroredRegion(std::optional<SectorCache::MirroredRegion> region)
    {
        mirrored = region;
    }

    void FatLiteWindow::Forget()
    {
        loaded = none;
        dirty = false;
    }

    void FatLiteWindow::Drop(uint32_t firstSector, uint32_t sectors)
    {
        if (loaded != none && loaded >= firstSector && loaded - firstSector < sectors)
            Forget();
    }

    void FatLiteWindow::Overlay(uint32_t firstSector, infra::ByteRange data) const
    {
        if (dirty && loaded >= firstSector && loaded - firstSector < data.size() / buffer.size())
            infra::Copy(buffer, infra::Head(infra::DiscardHead(data, (loaded - firstSector) * buffer.size()), buffer.size()));
    }

    void FatLiteWindow::Start(Access access, uint32_t sector, uint8_t rank)
    {
        this->access = access;
        requested = sector;
        requestedRank = rank;

        if (dirty && (sector != loaded || (access != Access::read && rank != this->rank)))
            WriteBack([this](Result result)
                {
                    if (result != Result::success)
                        Deliver(result);
                    else
                        Load();
                });
        else
            Load();
    }

    void FatLiteWindow::Load()
    {
        if (requested == loaded || access == Access::overwrite)
        {
            loaded = requested;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    Deliver(Result::success);
                });
        }
        else
        {
            loaded = none;
            barrier.Device().ReadBlocks(buffer, requested, [this](hal::BlockDevice::Result result)
                {
                    Loaded(result);
                });
        }
    }

    void FatLiteWindow::Loaded(hal::BlockDevice::Result result)
    {
        if (result == hal::BlockDevice::Result::success)
            loaded = requested;

        Deliver(ToResult(result));
    }

    void FatLiteWindow::Deliver(Result result)
    {
        if (access == Access::read)
        {
            onRead(result, buffer);
            return;
        }

        if (result == Result::success)
        {
            if (access == Access::overwrite)
                std::ranges::fill(buffer, uint8_t{ 0 });

            dirty = true;
            rank = requestedRank;
        }

        onModify(result, buffer);
    }

    void FatLiteWindow::WriteCopy()
    {
        auto sector = copy == 0 ? loaded : loaded + copy * mirrored->sectors;
        barrier.Write(buffer, sector, rank, [this](hal::BlockDevice::Result result)
            {
                CopyWritten(result);
            });
    }

    void FatLiteWindow::CopyWritten(hal::BlockDevice::Result result)
    {
        if (result != hal::BlockDevice::Result::success)
            onWrittenBack(ToResult(result));
        else if (++copy != Copies())
            WriteCopy();
        else
        {
            dirty = false;
            onWrittenBack(Result::success);
        }
    }

    uint8_t FatLiteWindow::Copies() const
    {
        if (mirrored && loaded >= mirrored->firstSector && loaded - mirrored->firstSector < mirrored->sectors)
            return mirrored->copies;

        return 1;
    }
}
