#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <utility>

namespace fs
{
    NorFile::NorFile(NorVolume& volume, infra::ByteRange buffer)
        : volume(volume)
        , buffer(buffer)
        , claimer(volume.Queue())
    {
        really_assert(buffer.size() >= NorFileBufferBytes(volume.Config()));
    }

    NorFile::~NorFile()
    {
        if (open)
            Detach();
    }

    void NorFile::Open(const NorOpenFile& opened)
    {
        state = opened;
        state.buffer = buffer;
        state.position = state.mode == OpenMode::append ? state.size : 0;
        generation = volume.Generation();
        open = true;
        volume.OpenFiles().push_back(*this);
    }

    infra::ByteRange NorFile::Buffer() const
    {
        return buffer;
    }

    NorOpenFile& NorFile::State()
    {
        return state;
    }

    const NorOpenFile& NorFile::State() const
    {
        return state;
    }

    void NorFile::Detach()
    {
        volume.OpenFiles().erase(*this);
        open = false;
    }

    uint32_t NorFile::Size() const
    {
        return state.size;
    }

    uint32_t NorFile::Position() const
    {
        return state.position;
    }

    void NorFile::Seek(uint32_t position)
    {
        state.position = std::min(position, state.size);
    }

    void NorFile::Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
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

    void NorFile::Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone)
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

    void NorFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !state.Writable())
            volume.Defer(onDone, Unwritable());
        else
            volume.Defer(onDone, Availability() == Result::success ? Result::unsupported : Availability());
    }

    void NorFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !state.Writable())
            volume.Defer(onDone, Unwritable());
        else
            volume.Defer(onDone, Availability());
    }

    void NorFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending())
            volume.Defer(onDone, Result::busy);
        else if (!state.Writable())
            volume.Defer(onDone, Availability());
        else
        {
            this->onDone = onDone;
            Claim(Call::sync);
        }
    }

    void NorFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending())
            volume.Defer(onDone, Result::busy);
        else if (!state.Writable())
        {
            volume.Defer(onDone, Availability());
            Forget();
        }
        else
        {
            this->onDone = onDone;
            Claim(Call::close);
        }
    }

    void NorFile::UnReferenced()
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

    bool NorFile::Pending() const
    {
        return call != Call::none;
    }

    Result NorFile::Unwritable() const
    {
        return Pending() ? Result::busy : Result::readOnly;
    }

    Result NorFile::Availability() const
    {
        return volume.Available(generation) ? Result::success : Result::notMounted;
    }

    void NorFile::Claim(Call claimed)
    {
        call = claimed;
        self = Share(*this);
        claimer.Claim([this]
            {
                Granted();
            });
    }

    void NorFile::Granted()
    {
        if (!volume.Available(generation))
        {
            if (call == Call::read)
                ReadDone(Result::notMounted, 0);
            else
                Done(Result::notMounted);
        }
        else if (call == Call::read)
            volume.Operation().emplace<NorRead>(volume, state, readBuffer, [this](Result result, std::size_t bytesRead)
                                  {
                                      ReadDone(result, bytesRead);
                                  })
                .Start();
        else if (call == Call::write)
            volume.Operation().emplace<NorWrite>(volume, state, writeData, [this](Result result)
                                  {
                                      Done(result);
                                  })
                .Start();
        else
            volume.FileCommit().Commit(state, [this](Result result)
                {
                    Done(result);
                });
    }

    void NorFile::ReadDone(Result result, std::size_t bytesRead)
    {
        claimer.Release();
        call = Call::none;
        self = nullptr;
        onRead(result, bytesRead);
    }

    void NorFile::Done(Result result)
    {
        auto completed = std::exchange(call, Call::none);
        claimer.Release();

        if (completed == Call::close || completed == Call::implicitClose)
        {
            if (result != Result::success && volume.Available(generation))
                ReleaseUncommittedBlocks();

            Forget();
        }

        if (completed == Call::implicitClose)
        {
            Release();
            return;
        }

        self = nullptr;
        onDone(result);
    }

    void NorFile::ReleaseUncommittedBlocks()
    {
        for (const auto& entry : state.entries)
            volume.State().blocks.Set(entry.block, NorBlockState::freeNotErased);

        state.entries.clear();
    }

    void NorFile::Forget()
    {
        if (open)
            Detach();
    }
}
