#include "spike/fat_reader/FatReaderWindow.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace spike
{
    FatWindow::FatWindow(hal::BlockDevice& device, infra::ByteRange buffer)
        : device(device)
        , buffer(buffer)
    {}

    hal::BlockDevice& FatWindow::Device() const
    {
        return device;
    }

    void FatWindow::Load(uint32_t sector, const infra::Function<void(fs::Result result)>& onDone)
    {
        this->onDone = onDone;

        if (sector == loaded)
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    this->onDone(fs::Result::success);
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

    infra::ConstByteRange FatWindow::Bytes() const
    {
        return buffer;
    }

    void FatWindow::Forget()
    {
        loaded = none;
    }

    void FatWindow::Loaded(hal::BlockDevice::Result result)
    {
        if (result == hal::BlockDevice::Result::success)
            loaded = requested;

        onDone(fs::ToResult(result));
    }

    FatChain::FatChain(const FatLayout& layout, FatWindow& window)
        : layout(layout)
        , window(window)
    {}

    void FatChain::Next(uint32_t cluster, const infra::Function<void(fs::Result result, uint32_t next)>& onDone)
    {
        this->cluster = cluster;
        this->onDone = onDone;
        offset = layout.kind == FatKind::fat12 ? cluster + cluster / 2 : cluster * (layout.kind == FatKind::fat16 ? 2 : 4);
        value = 0;
        collected = 0;
        Load();
    }

    void FatChain::Load()
    {
        window.Load(layout.fatStart + offset / fatSectorBytes, [this](fs::Result result)
            {
                Collect(result);
            });
    }

    void FatChain::Collect(fs::Result result)
    {
        if (result != fs::Result::success)
        {
            onDone(result, 0);
            return;
        }

        uint8_t width = layout.kind == FatKind::fat32 ? 4 : 2;

        do
        {
            value |= uint32_t{ window.Bytes()[offset % fatSectorBytes] } << (8 * collected);
            ++offset;
            ++collected;
        } while (collected != width && offset % fatSectorBytes != 0);

        if (collected != width)
            Load();
        else
            Decode();
    }

    void FatChain::Decode()
    {
        uint32_t end = 0x0ffffff8;

        if (layout.kind == FatKind::fat12)
        {
            value = (cluster & 1) != 0 ? value >> 4 : value & 0xfff;
            end = 0xff8;
        }
        else if (layout.kind == FatKind::fat16)
            end = 0xfff8;
        else
            value &= 0x0fffffff;

        if (value >= end)
            onDone(fs::Result::success, 0);
        else if (!IsCluster(layout, value))
            onDone(fs::Result::corrupted, 0);
        else
            onDone(fs::Result::success, value);
    }
}
