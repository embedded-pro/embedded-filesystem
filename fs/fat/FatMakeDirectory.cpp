#include "fs/fat/FatMakeDirectory.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    namespace
    {
        ShortEntry DotEntry(const ShortNameBytes& name, uint32_t firstCluster, FatDateTime now)
        {
            ShortEntry entry;
            entry.name.bytes = name;
            entry.attributes = FatAttributes::directory;
            entry.createTime = now.time;
            entry.createDate = now.date;
            entry.accessDate = now.date;
            entry.writeTime = now.time;
            entry.writeDate = now.date;
            entry.firstCluster = firstCluster;
            return entry;
        }
    }

    FatMakeDirectory::FatMakeDirectory(FatVolume& volume, const FatMakeDirectoryCall& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(call)
        , onFinished(onFinished)
        , parent(volume)
    {}

    FatMakeDirectory::~FatMakeDirectory() = default;

    void FatMakeDirectory::Start()
    {
        if (IsRoot(call.path))
        {
            Finish(Result::alreadyExists);
            return;
        }

        parent.Find(call.path, true, [this](Result result)
            {
                OnFound(result);
            });
    }

    void FatMakeDirectory::OnFound(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else if (parent.Found())
            Finish(Result::alreadyExists);
        else
            parent.PrepareAlias([this](Result result)
                {
                    OnPrepared(result);
                });
    }

    void FatMakeDirectory::OnPrepared(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else
            volume.Allocator().Grow(std::nullopt, 1, [this](Result result, uint32_t first, uint32_t)
                {
                    OnAllocated(result, first);
                });
    }

    void FatMakeDirectory::OnAllocated(Result result, uint32_t first)
    {
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        cluster = first;
        sector = volume.Geometry().SectorOfCluster(cluster);
        InitializeSector();
    }

    void FatMakeDirectory::InitializeSector()
    {
        volume.Cache().Overwrite(sector, dataRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success && sector == volume.Geometry().SectorOfCluster(cluster))
                    WriteDotEntries(contents);

                OnInitialized(result);
            });
    }

    void FatMakeDirectory::OnInitialized(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else if (++sector != volume.Geometry().SectorOfCluster(cluster) + volume.Geometry().sectorsPerCluster)
            InitializeSector();
        else
            parent.Write(FatAttributes::directory, cluster, [this](Result result)
                {
                    Finish(result);
                });
    }

    void FatMakeDirectory::WriteDotEntries(infra::ByteRange contents) const
    {
        auto now = volume.Timestamp();
        EncodeShortEntry(DotEntry({ '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }, cluster, now), infra::Head(contents, directoryEntryBytes));
        EncodeShortEntry(DotEntry({ '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }, parent.Cluster(), now), infra::Head(infra::DiscardHead(contents, directoryEntryBytes), directoryEntryBytes));
    }

    void FatMakeDirectory::Finish(Result result)
    {
        onFinished(result);
        call.onDone(result);
    }
}
