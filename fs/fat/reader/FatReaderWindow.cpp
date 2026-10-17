#include "fs/fat/reader/FatReaderWindow.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace fs
{
    FatReaderWindow::FatReaderWindow(hal::BlockDevice& device, infra::ByteRange buffer)
        : device(device)
        , buffer(buffer)
    {}

    FatReaderWindow::~FatReaderWindow() = default;

    uint32_t FatReaderWindow::SectorSize() const
    {
        return static_cast<uint32_t>(buffer.size());
    }

    void FatReaderWindow::Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone)
    {
        this->onDone = onDone;

        if (sector == loaded)
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    this->onDone(Result::success, buffer);
                });
        else
        {
            requested = sector;
            loaded = none;
            device.ReadBlocks(buffer, sector, [this](hal::BlockDevice::Result result)
                {
                    Loaded(result);
                });
        }
    }

    void FatReaderWindow::Forget()
    {
        loaded = none;
    }

    void FatReaderWindow::Loaded(hal::BlockDevice::Result result)
    {
        if (result == hal::BlockDevice::Result::success)
            loaded = requested;

        onDone(ToResult(result), buffer);
    }
}
