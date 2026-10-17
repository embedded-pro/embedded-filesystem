#include "fs/fat/coro/FatCoroWindow.hpp"

namespace fs
{
    FatCoroWindow::FatCoroWindow(hal::BlockDevice& device, infra::ByteRange buffer)
        : device(device)
        , buffer(buffer)
    {}

    FatCoroWindow::Load FatCoroWindow::Read(uint32_t sector)
    {
        return Load{ *this, sector };
    }

    void FatCoroWindow::Start(uint32_t sector, std::coroutine_handle<> awaiting)
    {
        loaded = none;
        requested = sector;
        handle = awaiting;
        device.ReadBlocks(buffer, sector, [this](hal::BlockDevice::Result readResult)
            {
                result = ToResult(readResult);

                if (result == Result::success)
                    loaded = requested;

                handle.resume();
            });
    }

    infra::ConstByteRange FatCoroWindow::Contents() const
    {
        return buffer;
    }

    uint32_t FatCoroWindow::SectorSize() const
    {
        return static_cast<uint32_t>(buffer.size());
    }

    void FatCoroWindow::Forget()
    {
        loaded = none;
    }
}
