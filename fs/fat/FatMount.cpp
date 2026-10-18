#include "fs/fat/FatMount.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatTable.hpp"

namespace fs
{
    FatMount::FatMount(hal::BlockDevice& device, SectorCache& cache, FatVolumeState& volume, FatCleanState readCleanState, const infra::Function<void(Result result)>& onDone)
        : device(device)
        , cache(cache)
        , volume(volume)
        , readCleanState(readCleanState)
        , onDone(onDone)
    {}

    FatMount::~FatMount() = default;

    void FatMount::Start()
    {
        volume = FatVolumeState{};
        cache.Clear();
        cache.SetMirroredRegion(std::nullopt);
        volumeSectors = device.NumberOfBlocks();

        if (device.BlockSize() != cache.SectorSize())
            Done(Result::unsupported);
        else
            Read();
    }

    void FatMount::Read()
    {
        cache.Read(volumeStart, [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatMount::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        if (auto parameters = ParseBootSector(contents))
            UseGeometry(*parameters);
        else if (partitioned)
            Done(Result::corrupted);
        else if (IsProtectiveMbr(contents))
            Done(Result::unsupported);
        else if (auto partition = FirstFatPartition(contents); partition && partition->firstSector < device.NumberOfBlocks() && partition->sectors <= device.NumberOfBlocks() - partition->firstSector)
        {
            partitioned = true;
            volumeStart = partition->firstSector;
            volumeSectors = partition->sectors;
            Read();
        }
        else
            Done(Result::corrupted);
    }

    void FatMount::UseGeometry(const BiosParameterBlock& parameters)
    {
        auto geometry = GeometryOf(parameters, volumeStart, volumeSectors, device.BlockSize());

        if (geometry.result != Result::success)
        {
            Done(geometry.result);
            return;
        }

        volume.geometry = geometry.geometry;

        if (volume.geometry.mirrored && volume.geometry.fatCopies > 1)
            cache.SetMirroredRegion(SectorCache::MirroredRegion{ volume.geometry.firstFatSector, volume.geometry.sectorsPerFat, volume.geometry.fatCopies });

        if (volume.geometry.type != FatType::fat32 || readCleanState == nullptr)
            Done(Result::success);
        else
            readCleanState(*this);
    }

    void FatMount::ReadCleanState()
    {
        fsInfoRead = !volume.geometry.fsInfoSector.has_value();
        ReadCleanSector();
    }

    void FatMount::ReadCleanSector()
    {
        cache.Read(fsInfoRead ? volume.geometry.ActiveFatSector() : *volume.geometry.fsInfoSector, [this](Result result, infra::ConstByteRange contents)
            {
                OnCleanSector(result, contents);
            });
    }

    void FatMount::OnCleanSector(Result result, infra::ConstByteRange contents)
    {
        if (fsInfoRead)
            OnFatHeader(result, contents);
        else if (result != Result::success)
            Done(result);
        else
        {
            volume.fsInfo = ParseFsInfo(contents, volume.geometry.clusterCount);
            fsInfoRead = true;
            ReadCleanSector();
        }
    }

    void FatMount::OnFatHeader(Result result, infra::ConstByteRange contents)
    {
        if (result == Result::success)
            volume.cleanAtMount = CleanShutdown(volume.geometry.type, ReadEntry(volume.geometry.type, 1, contents, infra::ConstByteRange()));

        if (volume.cleanAtMount == true && volume.fsInfo)
            volume.freeClusters = volume.fsInfo->freeClusters;

        Done(result);
    }

    void FatMount::Done(Result result)
    {
        onDone(result);
    }
}
