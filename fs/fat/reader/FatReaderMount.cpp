#include "fs/fat/reader/FatReaderMount.hpp"
#include "fs/fat/FatPartitionTable.hpp"

namespace fs
{
    FatReaderMount::FatReaderMount(hal::BlockDevice& device, SectorSource& source, FatGeometry& geometry)
        : device(device)
        , source(source)
        , geometry(geometry)
    {}

    FatReaderMount::~FatReaderMount() = default;

    void FatReaderMount::Start(const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        partitioned = false;
        volumeStart = 0;
        volumeSectors = device.NumberOfBlocks();

        if (device.BlockSize() != source.SectorSize())
            this->onDone(Result::unsupported);
        else
            Read();
    }

    void FatReaderMount::Read()
    {
        source.Read(volumeStart, [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatReaderMount::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        if (auto parameters = ParseBootSector(contents))
            UseGeometry(*parameters);
        else if (partitioned)
            onDone(Result::corrupted);
        else if (IsProtectiveMbr(contents))
            onDone(Result::unsupported);
        else if (auto partition = FirstFatPartition(contents); partition && partition->firstSector < device.NumberOfBlocks() && partition->sectors <= device.NumberOfBlocks() - partition->firstSector)
        {
            partitioned = true;
            volumeStart = partition->firstSector;
            volumeSectors = partition->sectors;
            Read();
        }
        else
            onDone(Result::corrupted);
    }

    void FatReaderMount::UseGeometry(const BiosParameterBlock& parameters)
    {
        auto layout = GeometryOf(parameters, volumeStart, volumeSectors, device.BlockSize());
        geometry = layout.geometry;
        onDone(layout.result);
    }
}
