#include "fs/fat/FatOpen.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatFile.hpp"
#include "fs/fat/FatName.hpp"
#include "fs/fat/FatVolume.hpp"
#include <utility>

namespace fs
{
    FatOpen::FatOpen(FatVolume& volume, FatOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , allocate(allocate)
        , onFinished(onFinished)
        , scan(volume.Reader())
        , writer(volume.Cache(), volume.Reader(), volume.Allocator(), volume.Geometry())
    {}

    FatOpen::~FatOpen() = default;

    void FatOpen::Start()
    {
        if (IsRoot(call.path))
        {
            Finish(Result::isADirectory);
            return;
        }

        auto split = SplitLast(call.path);
        name = split.name;
        volume.Walk().Find(split.parent, [this](Result result)
            {
                OnParent(result);
            });
    }

    void FatOpen::OnParent(Result result)
    {
        result = VerifyParent(result);

        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        exact = ExactShortName(name);
        alias = BasisName(name);
        auto slots = static_cast<uint8_t>(exact ? 1 : LongEntriesFor(name) + 1);
        auto basis = exact || call.mode == OpenMode::read ? std::nullopt : std::make_optional(alias);

        scan.Scan(volume.Walk().FoundCluster(), name, slots, basis, [this](Result result)
            {
                OnScanned(result);
            });
    }

    Result FatOpen::VerifyParent(Result result) const
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

    void FatOpen::OnScanned(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else if (scan.Found())
            OpenExisting(*scan.Found());
        else if (call.mode == OpenMode::read)
            Finish(Result::notFound);
        else
            Create();
    }

    void FatOpen::OpenExisting(const FatScannedEntry& found)
    {
        auto result = IsDirectory(found) ? Result::isADirectory : VerifyFile(found);

        if (result == Result::success)
            file = allocate();

        if (file != nullptr)
            file->Open(found, call.mode);
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        Finish(result);
    }

    Result FatOpen::VerifyFile(const FatScannedEntry& found) const
    {
        if (call.mode == OpenMode::write)
            return Result::unsupported;

        if (call.mode != OpenMode::read && (found.entry.attributes & FatAttributes::readOnly) != 0)
            return Result::readOnly;

        if (found.entry.size != 0 && !volume.Geometry().IsCluster(found.entry.firstCluster))
            return Result::corrupted;

        for (auto& open : volume.OpenFiles())
            if (open.State().entry == found.shortEntry && (open.State().mode != OpenMode::read || call.mode != OpenMode::read))
                return Result::busy;

        return Result::success;
    }

    void FatOpen::Create()
    {
        if (!scan.Slots())
        {
            Finish(Result::noSpace);
            return;
        }

        file = allocate();

        if (file == nullptr)
            Finish(Result::tooManyOpenFiles);
        else if (exact)
            CreateWith(*exact);
        else if (auto tail = scan.FreeTail())
            CreateWith(ShortName{ WithNumericTail(alias, *tail) });
        else
        {
            alias = HashedAlias(alias, name);
            scan.ScanForAlias(alias, [this](Result result)
                {
                    OnAliasScanned(result);
                });
        }
    }

    void FatOpen::OnAliasScanned(Result result)
    {
        if (result != Result::success)
            Finish(result);
        else if (scan.AliasInUse())
            Finish(Result::noSpace);
        else
            CreateWith(ShortName{ alias });
    }

    void FatOpen::CreateWith(const ShortName& shortName)
    {
        auto now = volume.Timestamp();
        ShortEntry entry;
        entry.name = shortName;
        entry.attributes = FatAttributes::archive;
        entry.createTime = now.time;
        entry.createDate = now.date;
        entry.accessDate = now.date;
        entry.writeTime = now.time;
        entry.writeDate = now.date;

        writer.Write(FatEntrySet{ exact ? infra::BoundedConstString() : name, entry }, *scan.Slots(), [this](Result result)
            {
                OnCreated(result);
            });
    }

    void FatOpen::OnCreated(Result result)
    {
        if (result == Result::success)
            file->Open(writer.Written(), call.mode);

        Finish(result);
    }

    void FatOpen::Finish(Result result)
    {
        auto opened = std::exchange(file, nullptr);
        onFinished(result);
        call.onDone(result, result == Result::success ? opened : nullptr);
    }
}
