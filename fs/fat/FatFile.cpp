#include "fs/fat/FatFile.hpp"
#include <algorithm>

namespace fs
{
    FatFile::FatFile(FatVolume& volume, FatExtentCache& extents)
        : volume(volume)
        , extents(extents)
        , claimer(volume.Queue())
    {}

    FatFile::~FatFile() = default;

    void FatFile::Open(const FatScannedEntry& scanned, OpenMode mode)
    {
        state = FatOpenFile{ scanned.shortEntry, scanned.entry.firstCluster, scanned.entry.size, 0, mode };
        generation = volume.Generation();
        open = true;
        extents.Clear();

        if (state.size == 0)
            return;

        extents.Add(FatExtent{ 0, state.firstCluster, 1 });

        if (auto last = volume.ClosedFiles().Recall(state.entry, state.firstCluster, state.size))
            extents.Add(*last);
    }

    uint32_t FatFile::Size() const
    {
        return state.size;
    }

    uint32_t FatFile::Position() const
    {
        return state.position;
    }

    void FatFile::Seek(uint32_t position)
    {
        state.position = std::min(position, state.size);
    }

    void FatFile::Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
    {
        if (onRead)
        {
            volume.Defer(onDone, Result::busy);
            return;
        }

        readBuffer = buffer;
        onRead = onDone;
        self = Share(*this);
        claimer.Claim([this]
            {
                StartRead();
            });
    }

    void FatFile::Write(infra::ConstByteRange, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unwritable());
    }

    void FatFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unwritable());
    }

    void FatFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unwritable());
    }

    void FatFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        if (onRead)
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, volume.Available(generation) ? Result::success : volume.Unavailable());
    }

    void FatFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        if (onRead)
        {
            volume.Defer(onDone, Result::busy);
            return;
        }

        auto result = volume.Available(generation) ? Result::success : volume.Unavailable();
        Forget();
        volume.Defer(onDone, result);
    }

    void FatFile::UnReferenced()
    {
        Forget();
        Release();
    }

    void FatFile::StartRead()
    {
        if (!volume.Available(generation))
        {
            ReadDone(volume.Unavailable(), 0);
            return;
        }

        volume.Operation().emplace<FatRead>(volume, state, extents, readBuffer, [this](Result result, std::size_t bytesRead)
                              {
                                  volume.Report(result);
                                  ReadDone(result, bytesRead);
                              })
            .Start();
    }

    void FatFile::ReadDone(Result result, std::size_t bytesRead)
    {
        claimer.Release();
        self = nullptr;
        onRead(result, bytesRead);
    }

    Result FatFile::Unwritable() const
    {
        return onRead ? Result::busy : Result::readOnly;
    }

    void FatFile::Forget()
    {
        if (open && volume.Available(generation))
            if (auto last = extents.Last())
                volume.ClosedFiles().Remember(FatClosedFile{ state.entry, state.firstCluster, state.size, *last });

        open = false;
    }
}
