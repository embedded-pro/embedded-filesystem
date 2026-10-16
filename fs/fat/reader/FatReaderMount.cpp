#include "fs/fat/reader/FatReaderMount.hpp"
#include "fs/fat/FatPartitionTable.hpp"

namespace fs
{
    FatReaderMount::FatReaderMount(hal::BlockDevice& device, FatReaderWindow& window, FatGeometry& geometry)
        : device(device)
        , window(window)
        , geometry(geometry)
    {}

    FatReaderMount::~FatReaderMount() = default;

    void FatReaderMount::Start(const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        window.Forget();

        if (device.BlockSize() != window.SectorSize())
            this->onDone(Result::unsupported);
        else
            window.Read(0, [this](Result result, infra::ConstByteRange contents)
                {
                    OnFirstSector(result, contents);
                });
    }

    void FatReaderMount::OnFirstSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        if (auto parameters = ParseBootSector(contents))
            UseGeometry(*parameters, 0, device.NumberOfBlocks());
        else if (IsProtectiveMbr(contents))
            onDone(Result::unsupported);
        else if (auto partition = FirstFatPartition(contents); partition && partition->firstSector < device.NumberOfBlocks() && partition->sectors <= device.NumberOfBlocks() - partition->firstSector)
            ReadBootSector(partition->firstSector, partition->sectors);
        else
            onDone(Result::corrupted);
    }

    void FatReaderMount::ReadBootSector(uint32_t sector, uint32_t sectors)
    {
        volumeStart = sector;
        volumeSectors = sectors;
        window.Read(sector, [this](Result result, infra::ConstByteRange contents)
            {
                OnBootSector(result, contents);
            });
    }

    void FatReaderMount::OnBootSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
            onDone(result);
        else if (auto parameters = ParseBootSector(contents))
            UseGeometry(*parameters, volumeStart, volumeSectors);
        else
            onDone(Result::corrupted);
    }

    void FatReaderMount::UseGeometry(const BiosParameterBlock& parameters, uint32_t start, uint32_t sectors)
    {
        auto layout = GeometryOf(parameters, start, sectors, device.BlockSize());
        geometry = layout.geometry;
        onDone(layout.result);
    }
}
