#include "fs/fat/FatFile.hpp"
#include <algorithm>
#include <utility>

namespace fs
{
    FatFile::FatFile(FatVolume& volume, FatExtentCache& extents)
        : volume(volume)
        , extents(extents)
        , claimer(volume.Queue())
    {}

    FatFile::~FatFile()
    {
        if (open)
            Detach();
    }

    void FatFile::Open(const FatScannedEntry& scanned, OpenMode mode)
    {
        auto clusterBytes = volume.Geometry().sectorsPerCluster * volume.Cache().SectorSize();
        state = FatOpenFile{};
        state.entry = scanned.shortEntry;
        state.firstCluster = scanned.entry.firstCluster;
        state.size = scanned.entry.size;
        state.clusters = static_cast<uint32_t>((uint64_t{ state.size } + clusterBytes - 1) / clusterBytes);
        state.mode = mode;
        state.position = mode == OpenMode::append ? state.size : 0;
        state.Committed();
        generation = volume.Generation();
        open = true;
        volume.OpenFiles().push_back(*this);
        extents.Clear();

        if (state.clusters == 0)
            return;

        extents.Add(FatExtent{ 0, state.firstCluster, 1 });

        if (auto last = volume.ClosedFiles().Recall(state.entry, state.firstCluster, state.size))
            extents.Add(*last);
    }

    FatOpenFile& FatFile::State()
    {
        return state;
    }

    const FatOpenFile& FatFile::State() const
    {
        return state;
    }

    FatExtentCache& FatFile::Extents()
    {
        return extents;
    }

    void FatFile::Detach()
    {
        volume.OpenFiles().erase(*this);
        open = false;
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
        if (Pending())
        {
            volume.Defer(onDone, Result::busy);
            return;
        }

        readBuffer = buffer;
        onRead = onDone;
        Claim(Call::read);
    }

    void FatFile::Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !state.Writable())
        {
            volume.Defer(onDone, Unwritable());
            return;
        }

        writeData = data;
        this->onDone = onDone;
        Claim(Call::write);
    }

    void FatFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !state.Writable())
        {
            volume.Defer(onDone, Unwritable());
            return;
        }

        this->onDone = onDone;
        Claim(Call::truncate);
    }

    void FatFile::ExpectSize(uint32_t size, const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !state.Writable())
        {
            volume.Defer(onDone, Unwritable());
            return;
        }

        expectedSize = size;
        this->onDone = onDone;
        Claim(Call::expectSize);
    }

    void FatFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending())
            volume.Defer(onDone, Result::busy);
        else if (!state.Writable())
            volume.Defer(onDone, volume.Available(generation) ? Result::success : volume.Unavailable());
        else
        {
            this->onDone = onDone;
            Claim(Call::sync);
        }
    }

    void FatFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending())
            volume.Defer(onDone, Result::busy);
        else if (!state.Writable())
        {
            auto result = volume.Available(generation) ? Result::success : volume.Unavailable();
            Forget();
            volume.Defer(onDone, result);
        }
        else
        {
            this->onDone = onDone;
            Claim(Call::close);
        }
    }

    void FatFile::UnReferenced()
    {
        if (open && state.Writable() && state.Uncommitted() && volume.Available(generation))
        {
            call = Call::implicitClose;
            claimer.Claim([this]
                {
                    Granted();
                });
            return;
        }

        Forget();
        Release();
    }

    bool FatFile::Pending() const
    {
        return call != Call::none;
    }

    void FatFile::Claim(Call call)
    {
        this->call = call;
        self = Share(*this);
        claimer.Claim([this]
            {
                Granted();
            });
    }

    void FatFile::Granted()
    {
        if (!volume.Available(generation))
        {
            if (call == Call::read)
                ReadDone(volume.Unavailable(), 0);
            else
                Done(volume.Unavailable());
        }
        else if (call == Call::read)
            StartRead();
        else if (call == Call::write)
            StartWrite();
        else if (call == Call::truncate)
            StartTruncate();
        else if (call == Call::expectSize)
            StartExpectSize();
        else
            StartSync();
    }

    void FatFile::StartRead()
    {
        volume.Operation().emplace<FatRead>(volume, state, extents, readBuffer, [this](Result result, std::size_t bytesRead)
                              {
                                  volume.Report(result);
                                  ReadDone(result, bytesRead);
                              })
            .Start();
    }

    void FatFile::StartWrite()
    {
        volume.Operation().emplace<FatWrite>(volume, state, extents, writeData, [this](Result result)
                              {
                                  volume.Report(result);
                                  Done(result);
                              })
            .Start();
    }

    void FatFile::StartTruncate()
    {
        volume.Operation().emplace<FatTruncation>(volume).Truncate(state, extents, [this](Result result)
            {
                volume.Report(result);
                Done(result);
            });
    }

    void FatFile::StartExpectSize()
    {
        volume.Operation().emplace<FatExpectSize>(volume, state, extents, expectedSize, [this](Result result)
                              {
                                  volume.Report(result);
                                  Done(result);
                              })
            .Start();
    }

    void FatFile::StartSync()
    {
        volume.Operation().emplace<FatSync>(volume, state, [this](Result result)
                              {
                                  volume.Report(result);
                                  Done(result);
                              })
            .Start();
    }

    void FatFile::ReadDone(Result result, std::size_t bytesRead)
    {
        claimer.Release();
        call = Call::none;
        self = nullptr;
        onRead(result, bytesRead);
    }

    void FatFile::Done(Result result)
    {
        auto completed = std::exchange(call, Call::none);
        claimer.Release();

        if (completed == Call::close || completed == Call::implicitClose)
            Forget();

        if (completed == Call::implicitClose)
        {
            Release();
            return;
        }

        self = nullptr;
        onDone(result);
    }

    Result FatFile::Unwritable() const
    {
        return Pending() ? Result::busy : Result::readOnly;
    }

    void FatFile::Forget()
    {
        if (!open)
            return;

        if (volume.Available(generation))
            if (auto last = extents.Last())
                volume.ClosedFiles().Remember(FatClosedFile{ state.entry, state.firstCluster, state.size, *last });

        Detach();
    }
}
