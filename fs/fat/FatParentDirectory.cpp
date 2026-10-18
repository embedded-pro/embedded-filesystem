#include "fs/fat/FatParentDirectory.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatLongNames.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/FatVolume.hpp"

namespace fs
{
    FatParentDirectory::FatParentDirectory(FatVolume& volume)
        : volume(volume)
        , scan(volume.Reader(), volume.Operations().longNames)
        , writer(volume.Cache(), volume.Reader(), volume.Allocator(), volume.Geometry(), volume.Operations().longNames)
        , resume([this](Result result)
              {
                  Resume(result);
              })
    {}

    FatParentDirectory::~FatParentDirectory() = default;

    void FatParentDirectory::Find(infra::BoundedConstString path, bool forCreation, const infra::Function<void(Result result)>& onDone)
    {
        this->forCreation = forCreation;
        except = std::nullopt;
        Locate(path, 0, onDone);
    }

    void FatParentDirectory::FindTarget(infra::BoundedConstString path, const FatScannedEntry& source, const infra::Function<void(Result result)>& onDone)
    {
        forCreation = true;
        except = source.shortEntry;
        Locate(path, IsDirectory(source) ? source.entry.firstCluster : 0, onDone);
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
        const auto* longNames = volume.Operations().longNames;

        if (!exact && longNames == nullptr)
            this->onDone(Result::unsupported);
        else if (!scan.Slots())
            this->onDone(Result::noSpace);
        else if (exact)
            Prepared(*exact);
        else if (auto tail = scan.FreeTail(); tail.has_value())
            Prepared(ShortName{ longNames->withTail(alias, *tail) });
        else
        {
            alias = longNames->hashed(alias, name);
            scan.ScanForAlias(alias, Then(Step::aliasScanned));
        }
    }

    void FatParentDirectory::Write(uint8_t attributes, uint32_t firstCluster, const infra::Function<void(Result result)>& onDone)
    {
        auto now = volume.Timestamp();
        ShortEntry entry;
        entry.attributes = attributes;
        entry.createTime = now.time;
        entry.createDate = now.date;
        entry.accessDate = now.date;
        entry.writeTime = now.time;
        entry.writeDate = now.date;
        entry.firstCluster = firstCluster;
        WriteEntry(entry, onDone);
    }

    void FatParentDirectory::WriteEntry(ShortEntry entry, const infra::Function<void(Result result)>& onDone)
    {
        entry.name = shortName;
        writer.Write(FatEntrySet{ exact ? infra::BoundedConstString() : name, entry }, *scan.Slots(), onDone);
    }

    FatScannedEntry FatParentDirectory::Written() const
    {
        return writer.Written();
    }

    void FatParentDirectory::Locate(infra::BoundedConstString path, uint32_t outside, const infra::Function<void(Result result)>& onDone)
    {
        auto split = SplitLast(path);
        name = split.name;
        this->onDone = onDone;
        volume.Walk().FindOutside(split.parent, outside, Then(Step::parentFound));
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
        const auto* longNames = volume.Operations().longNames;
        uint8_t slots = 1;
        std::optional<ShortNameBytes> basis;

        if (!exact && longNames != nullptr)
        {
            slots = static_cast<uint8_t>(longNames->entriesFor(name) + 1);
            alias = longNames->basis(name);

            if (forCreation)
                basis = alias;
        }

        scan.Scan(cluster, name, slots, basis, except, Then(Step::done));
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
        volume.MarkDirty(Then(Step::done));
    }

    const infra::Function<void(Result result)>& FatParentDirectory::Then(Step next)
    {
        step = next;
        return resume;
    }

    void FatParentDirectory::Resume(Result result)
    {
        switch (step)
        {
            case Step::parentFound:
                OnParent(result);
                break;
            case Step::aliasScanned:
                OnAliasScanned(result);
                break;
            default:
                onDone(result);
                break;
        }
    }
}
