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

        if (device.BlockSize() != cache.SectorSize())
            Done(Result::unsupported);
        else
            ReadFirstSector();
    }

    void FatMount::ReadFirstSector()
    {
        cache.Read(0, [this](Result result, infra::ConstByteRange contents)
            {
                OnFirstSector(result, contents);
            });
    }

    void FatMount::OnFirstSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        if (auto parameters = ParseBootSector(contents))
            UseGeometry(*parameters, 0, device.NumberOfBlocks());
        else if (IsProtectiveMbr(contents))
            Done(Result::unsupported);
        else if (auto partition = FirstFatPartition(contents); partition && partition->firstSector < device.NumberOfBlocks() && partition->sectors <= device.NumberOfBlocks() - partition->firstSector)
            ReadBootSector(partition->firstSector, partition->sectors);
        else
            Done(Result::corrupted);
    }

    void FatMount::ReadBootSector(uint32_t sector, uint32_t sectors)
    {
        volumeStart = sector;
        volumeSectors = sectors;
        cache.Read(sector, [this](Result result, infra::ConstByteRange contents)
            {
                OnBootSector(result, contents);
            });
    }

    void FatMount::OnBootSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
            Done(result);
        else if (auto parameters = ParseBootSector(contents))
            UseGeometry(*parameters, volumeStart, volumeSectors);
        else
            Done(Result::corrupted);
    }

    void FatMount::UseGeometry(const BiosParameterBlock& parameters, uint32_t start, uint32_t sectors)
    {
        auto geometry = GeometryOf(parameters, start, sectors, device.BlockSize());

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
        if (volume.geometry.fsInfoSector.has_value())
            ReadFsInfo();
        else
            ReadFatHeader();
    }

    void FatMount::ReadFsInfo()
    {
        cache.Read(*volume.geometry.fsInfoSector, [this](Result result, infra::ConstByteRange contents)
            {
                OnFsInfo(result, contents);
            });
    }

    void FatMount::OnFsInfo(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        volume.fsInfo = ParseFsInfo(contents, volume.geometry.clusterCount);
        ReadFatHeader();
    }

    void FatMount::ReadFatHeader()
    {
        cache.Read(volume.geometry.ActiveFatSector(), [this](Result result, infra::ConstByteRange contents)
            {
                OnFatHeader(result, contents);
            });
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
