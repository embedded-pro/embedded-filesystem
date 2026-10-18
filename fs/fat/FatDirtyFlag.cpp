#include "fs/fat/FatDirtyFlag.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatTable.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace fs
{
    FatDirtyFlag::FatDirtyFlag(SectorCache& cache, const FatVolumeState& volume)
        : cache(cache)
        , volume(volume)
    {}

    FatDirtyFlag::~FatDirtyFlag() = default;

    void FatDirtyFlag::Reset()
    {
        mark = Mark::untouched;
    }

    void FatDirtyFlag::MarkDirty(const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;

        if (mark != Mark::untouched || volume.geometry.type == FatType::fat12)
            CompleteLater();
        else
            cache.Read(FirstSector(), [this](Result result, infra::ConstByteRange contents)
                {
                    OnFirstSector(result, contents);
                });
    }

    void FatDirtyFlag::MarkClean(const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;

        if (mark != Mark::markedDirty)
            CompleteLater();
        else
            ModifyFirstSector();
    }

    void FatDirtyFlag::Skip(const infra::Function<void(Result result)>& onDone)
    {
        this->onDone = onDone;
        CompleteLater();
    }

    void FatDirtyFlag::OnFirstSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
            onDone(result);
        else if (CleanShutdown(volume.geometry.type, ReadEntry(volume.geometry.type, 1, contents, infra::ConstByteRange())) != true)
        {
            mark = Mark::leftDirty;
            onDone(Result::success);
        }
        else
            ModifyFirstSector();
    }

    void FatDirtyFlag::ModifyFirstSector()
    {
        cache.Modify(FirstSector(), mark == Mark::markedDirty ? cleanRank : tableRank, [this](Result result, infra::ByteRange contents)
            {
                OnFirstSectorModified(result, contents);
            });
    }

    void FatDirtyFlag::OnFirstSectorModified(Result result, infra::ByteRange contents)
    {
        if (result != Result::success)
            onDone(result);
        else if (mark == Mark::markedDirty)
        {
            WriteCleanBit(contents, true);
            mark = Mark::untouched;
            onDone(result);
        }
        else
        {
            WriteCleanBit(contents, false);
            cache.Flush([this](Result result)
                {
                    OnFlushed(result);
                });
        }
    }

    void FatDirtyFlag::OnFlushed(Result result)
    {
        if (result == Result::success)
            mark = Mark::markedDirty;
        else
            cache.Discard(FirstSector(), FirstSector() + 1);

        onDone(result);
    }

    void FatDirtyFlag::WriteCleanBit(infra::ByteRange contents, bool clean) const
    {
        auto type = volume.geometry.type;
        auto entry = ReadEntry(type, 1, contents, infra::ConstByteRange());

        WriteEntry(type, 1, WithCleanShutdown(type, entry, clean), contents, infra::ByteRange());
    }

    void FatDirtyFlag::CompleteLater()
    {
        infra::EventDispatcher::Instance().Schedule([this]
            {
                onDone(Result::success);
            });
    }

    uint32_t FatDirtyFlag::FirstSector() const
    {
        return volume.geometry.ActiveFatSector();
    }
}
