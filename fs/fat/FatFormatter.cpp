#include "fs/fat/FatFormatter.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t sectorBytes = 512;
        constexpr uint32_t fsInfoOffset = 1;
        constexpr uint32_t backupOffset = 6;
        constexpr uint32_t rootCluster = 2;
    }

    FatFormatter::FatFormatter(hal::BlockDevice& device, infra::ByteRange buffer)
        : device(device)
        , buffer(buffer)
    {
        really_assert(buffer.size() >= sectorBytes && buffer.size() % sectorBytes == 0);
    }

    void FatFormatter::Format(const FatFormatConfig& config, const infra::Function<void(Result result)>& onDone)
    {
        really_assert(!this->onDone);

        this->onDone = onDone;
        copy = 0;
        layout = ComputeFormatLayout(device.NumberOfBlocks(), device.BlockSize(), config);

        if (!layout)
            Complete(Result::unsupported);
        else if (layout->partition)
            WriteMbr();
        else
            WriteBootSector();
    }

    void FatFormatter::WriteMbr()
    {
        SerializeMbr(*layout->partition, FirstSector());
        WriteBuffer(0, 1, Step::bootSector);
    }

    void FatFormatter::WriteBootSector()
    {
        SerializeBootSector(layout->parameters, layout->type, FirstSector());
        WriteBuffer(layout->volumeStart, 1, IsFat32() ? Step::fsInfo : Step::fatStart);
    }

    void FatFormatter::WriteFsInfo()
    {
        SerializeFsInfo(FsInfo{ layout->clusterCount - 1, rootCluster + 1 }, FirstSector());
        WriteBuffer(layout->volumeStart + fsInfoOffset, 1, Step::backupBootSector);
    }

    void FatFormatter::WriteBackupBootSector()
    {
        SerializeBootSector(layout->parameters, layout->type, FirstSector());
        WriteBuffer(layout->volumeStart + backupOffset, 1, Step::backupFsInfo);
    }

    void FatFormatter::WriteBackupFsInfo()
    {
        SerializeFsInfo(FsInfo{ layout->clusterCount - 1, rootCluster + 1 }, FirstSector());
        WriteBuffer(layout->volumeStart + backupOffset + fsInfoOffset, 1, Step::fatStart);
    }

    void FatFormatter::WriteFatStart()
    {
        auto sector = FirstSector();
        std::ranges::fill(sector, uint8_t{ 0 });
        WriteEntry(layout->type, 0, MediaEntry(layout->type, layout->parameters.media), sector, infra::ByteRange());
        WriteEntry(layout->type, 1, EndOfChain(layout->type), sector, infra::ByteRange());

        if (IsFat32())
            WriteEntry(layout->type, rootCluster, EndOfChain(layout->type), sector, infra::ByteRange());

        WriteBuffer(FatSector(), 1, Step::fatRest);
    }

    void FatFormatter::ZeroFatRest()
    {
        ZeroSectors(FatSector() + 1, FatSector() + layout->parameters.sectorsPerFat, Step::nextFatCopy);
    }

    void FatFormatter::NextFatCopy()
    {
        if (++copy != layout->parameters.fatCopies)
            WriteFatStart();
        else
            ZeroRoot();
    }

    void FatFormatter::ZeroRoot()
    {
        auto rootStart = layout->volumeStart + layout->parameters.reservedSectors + layout->parameters.fatCopies * layout->parameters.sectorsPerFat;
        auto rootEnd = IsFat32() ? layout->dataStart + layout->parameters.sectorsPerCluster : layout->dataStart;

        ZeroSectors(rootStart, rootEnd, Step::discardData);
    }

    void FatFormatter::DiscardData()
    {
        auto begin = IsFat32() ? layout->dataStart + layout->parameters.sectorsPerCluster : layout->dataStart;

        afterDevice = Step::flushDevice;
        device.EraseBlocks(begin, device.NumberOfBlocks(), [this](hal::BlockDevice::Result deviceResult)
            {
                OnDevice(deviceResult);
            });
    }

    void FatFormatter::FlushDevice()
    {
        afterDevice = Step::complete;
        device.Flush([this](hal::BlockDevice::Result deviceResult)
            {
                OnDevice(deviceResult);
            });
    }

    void FatFormatter::WriteBuffer(uint32_t sector, uint32_t sectors, Step next)
    {
        afterDevice = next;
        device.WriteBlocks(infra::Head(buffer, sectors * sectorBytes), sector, [this](hal::BlockDevice::Result deviceResult)
            {
                OnDevice(deviceResult);
            });
    }

    void FatFormatter::ZeroSectors(uint32_t begin, uint32_t end, Step next)
    {
        std::ranges::fill(buffer, uint8_t{ 0 });
        zeroNext = begin;
        zeroEnd = end;
        afterZeroed = next;
        ZeroNextChunk();
    }

    void FatFormatter::ZeroNextChunk()
    {
        if (zeroNext == zeroEnd)
        {
            Continue(afterZeroed);
            return;
        }

        auto sectors = std::min(static_cast<uint32_t>(buffer.size() / sectorBytes), zeroEnd - zeroNext);
        auto sector = zeroNext;
        zeroNext += sectors;
        WriteBuffer(sector, sectors, Step::zeroChunk);
    }

    void FatFormatter::OnDevice(hal::BlockDevice::Result deviceResult)
    {
        if (deviceResult != hal::BlockDevice::Result::success)
            Complete(ToResult(deviceResult));
        else
            Continue(afterDevice);
    }

    void FatFormatter::Continue(Step next)
    {
        switch (next)
        {
            case Step::bootSector:
                WriteBootSector();
                break;
            case Step::fsInfo:
                WriteFsInfo();
                break;
            case Step::backupBootSector:
                WriteBackupBootSector();
                break;
            case Step::backupFsInfo:
                WriteBackupFsInfo();
                break;
            case Step::fatStart:
                WriteFatStart();
                break;
            case Step::fatRest:
                ZeroFatRest();
                break;
            case Step::zeroChunk:
                ZeroNextChunk();
                break;
            case Step::nextFatCopy:
                NextFatCopy();
                break;
            case Step::discardData:
                DiscardData();
                break;
            case Step::flushDevice:
                FlushDevice();
                break;
            default:
                Complete(Result::success);
                break;
        }
    }

    void FatFormatter::Complete(Result completionResult)
    {
        result = completionResult;

        infra::EventDispatcher::Instance().Schedule([this]
            {
                onDone(result);
            });
    }

    infra::ByteRange FatFormatter::FirstSector() const
    {
        return infra::Head(buffer, sectorBytes);
    }

    uint32_t FatFormatter::FatSector() const
    {
        return layout->volumeStart + layout->parameters.reservedSectors + copy * layout->parameters.sectorsPerFat;
    }

    bool FatFormatter::IsFat32() const
    {
        return layout->type == FatType::fat32;
    }
}
