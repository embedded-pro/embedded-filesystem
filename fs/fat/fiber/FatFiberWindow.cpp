#include "fs/fat/fiber/FatFiberWindow.hpp"

namespace fs
{
    FatFiberWindow::FatFiberWindow(fiber::Runner& runner, infra::ByteRange buffer)
        : runner(runner)
        , buffer(buffer)
    {}

    Result FatFiberWindow::Read(uint32_t sector)
    {
        if (loaded == sector)
            return Result::success;

        loaded = none;
        auto result = runner.Read(buffer, sector);

        if (result == Result::success)
            loaded = sector;

        return result;
    }

    infra::ConstByteRange FatFiberWindow::Contents() const
    {
        return buffer;
    }

    uint32_t FatFiberWindow::SectorSize() const
    {
        return static_cast<uint32_t>(buffer.size());
    }

    void FatFiberWindow::Forget()
    {
        loaded = none;
    }
}
