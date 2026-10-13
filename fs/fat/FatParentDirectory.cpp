#include "fs/fat/FatParentDirectory.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatParentDirectory::FatParentDirectory(FatVolume& volume)
        : volume(volume)
        , scan(volume.Reader())
        , writer(volume.Cache(), volume.Reader(), volume.Allocator(), volume.Geometry())
    {}

    FatParentDirectory::~FatParentDirectory() = default;

    void FatParentDirectory::Find(infra::BoundedConstString path, bool forCreation, const infra::Function<void(Result result)>& onDone)
    {
        auto split = SplitLast(path);
        name = split.name;
        this->forCreation = forCreation;
        this->onDone = onDone;
        volume.Walk().Find(split.parent, [this](Result result)
            {
                OnParent(result);
            });
    }

    const std::optional<FatScannedEntry>& FatParentDirectory::Found() const
    {
        return scan.Found();
    }

    uint32_t FatParentDirectory::Cluster() const
    {
        return cluster;
    }

    void FatParentDirectory::PrepareAlias(const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;

        if (!scan.Slots())
            this->onDone(Result::noSpace);
        else if (exact)
            Prepared(*exact);
        else if (auto tail = scan.FreeTail(); tail.has_value())
            Prepared(ShortName{ WithNumericTail(alias, *tail) });
        else
        {
            alias = HashedAlias(alias, name);
            scan.ScanForAlias(alias, [this](Result result)
                {
                    OnAliasScanned(result);
                });
        }
    }

    void FatParentDirectory::Write(uint8_t attributes, uint32_t firstCluster, const infra::Function<void(Result result)>& onDone)
    {
        auto now = volume.Timestamp();
        ShortEntry entry;
        entry.name = shortName;
        entry.attributes = attributes;
        entry.createTime = now.time;
        entry.createDate = now.date;
        entry.accessDate = now.date;
        entry.writeTime = now.time;
        entry.writeDate = now.date;
        entry.firstCluster = firstCluster;

        writer.Write(FatEntrySet{ exact ? infra::BoundedConstString() : name, entry }, *scan.Slots(), onDone);
    }

    FatScannedEntry FatParentDirectory::Written() const
    {
        return writer.Written();
    }

    void FatParentDirectory::OnParent(Result result)
    {
        result = VerifyParent(result);

        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        cluster = volume.Walk().FoundCluster();
        exact = ExactShortName(name);
        alias = BasisName(name);
        auto slots = static_cast<uint8_t>(exact ? 1 : LongEntriesFor(name) + 1);
        auto basis = exact || !forCreation ? std::nullopt : std::make_optional(alias);

        scan.Scan(cluster, name, slots, basis, [this](Result result)
            {
                onDone(result);
            });
    }

    Result FatParentDirectory::VerifyParent(Result result) const
    {
        const auto& walk = volume.Walk();

        if (result != Result::success)
            return result;

        if (!walk.FoundDirectory())
            return Result::notADirectory;

        if (!walk.FoundRoot() && !volume.Geometry().IsCluster(walk.FoundCluster()))
            return Result::corrupted;

        return ValidateFatName(name);
    }

    void FatParentDirectory::OnAliasScanned(Result result)
    {
        if (result != Result::success)
            onDone(result);
        else if (scan.AliasInUse())
            onDone(Result::noSpace);
        else
            Prepared(ShortName{ alias });
    }

    void FatParentDirectory::Prepared(const ShortName& prepared)
    {
        shortName = prepared;
        onDone(Result::success);
    }
}
