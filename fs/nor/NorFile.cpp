#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>

namespace fs
{
    NorFile::NorFile(NorVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    NorFile::~NorFile() = default;

    void NorFile::Open(uint32_t inode, uint32_t size, OpenMode mode)
    {
        state = NorOpenFile{ inode, size, 0, mode };
        generation = volume.Generation();
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
        if (reading)
        {
            volume.Defer(onDone, Result::busy);
            return;
        }

        reading = true;
        readBuffer = buffer;
        onRead = onDone;
        self = Share(*this);
        claimer.Claim([this]
            {
                Granted();
            });
    }

    void NorFile::Write(infra::ConstByteRange, const infra::Function<void(Result result)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        Report(onDone);
    }

    void NorFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        Report(onDone);
    }

    void NorFile::UnReferenced()
    {
        Release();
    }

    void NorFile::Refuse(const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, reading ? Result::busy : Result::readOnly);
    }

    void NorFile::Report(const infra::Function<void(Result result)>& onDone)
    {
        if (reading)
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, volume.Available(generation) ? Result::success : Result::notMounted);
    }

    void NorFile::Granted()
    {
        if (!volume.Available(generation))
            ReadDone(Result::notMounted, 0);
        else
            volume.Operation().emplace<NorRead>(volume, state, readBuffer, [this](Result result, std::size_t bytesRead)
                                  {
                                      ReadDone(result, bytesRead);
                                  })
                .Start();
    }

    void NorFile::ReadDone(Result result, std::size_t bytesRead)
    {
        claimer.Release();
        reading = false;
        self = nullptr;
        onRead(result, bytesRead);
    }
}
