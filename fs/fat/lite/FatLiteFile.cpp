#include "fs/fat/lite/FatLiteFile.hpp"
#include "fs/common/BlockDeviceResult.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatRanks.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/FatTimestamp.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace fs
{
    namespace
    {
        constexpr uint32_t maxSize = std::numeric_limits<uint32_t>::max();

        void MarkWritten(ShortEntry& entry)
        {
            entry.attributes |= FatAttributes::archive;
            entry.writeTime = fatEpoch.time;
            entry.writeDate = fatEpoch.date;
        }
    }

    FatLiteFile::FatLiteFile(FatLiteVolume& volume)
        : volume(volume)
        , claimer(volume.Queue())
    {}

    FatLiteFile::~FatLiteFile()
    {
        Detach();
    }

    void FatLiteFile::Open(const FatScannedEntry& scanned, OpenMode openMode)
    {
        generation = volume.Generation();
        mode = openMode;
        entry = scanned.shortEntry;
        firstCluster = scanned.entry.firstCluster;
        size = scanned.entry.size;
        position = mode == OpenMode::append ? size : 0;
        committedFirstCluster = firstCluster;
        committedSize = size;
        written = false;
        cluster = 0;
        clusterIndex = 0;
        open = true;
        volume.OpenFiles().push_back(*this);
    }

    void FatLiteFile::Detach()
    {
        if (!open)
            return;

        volume.OpenFiles().erase(*this);
        open = false;
    }

    bool FatLiteFile::Conflicts(FatEntryPosition position, OpenMode otherMode) const
    {
        return entry == position && (mode != OpenMode::read || otherMode != OpenMode::read);
    }

    bool FatLiteFile::Uncommitted() const
    {
        return firstCluster != committedFirstCluster || size != committedSize || written;
    }

    void FatLiteFile::Commit(const infra::Function<void(Result result)>& onDone)
    {
        onCommitted = onDone;
        volume.Window().Modify(volume.Reader().SectorOf(entry), entryRank, [this](Result result, infra::ByteRange contents)
            {
                if (result == Result::success)
                {
                    auto raw = infra::Head(infra::DiscardHead(contents, entry.index % (contents.size() / directoryEntryBytes) * directoryEntryBytes), directoryEntryBytes);
                    auto updated = DecodeShortEntry(raw);
                    updated.size = size;
                    updated.firstCluster = firstCluster;

                    if (written)
                        MarkWritten(updated);

                    EncodeShortEntry(updated, raw);
                    committedFirstCluster = firstCluster;
                    committedSize = size;
                    written = false;
                }

                onCommitted(result);
            });
    }

    uint32_t FatLiteFile::Size() const
    {
        return size;
    }

    uint32_t FatLiteFile::Position() const
    {
        return position;
    }

    void FatLiteFile::Seek(uint32_t newPosition)
    {
        position = std::min(newPosition, size);
    }

    void FatLiteFile::Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
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

    void FatLiteFile::Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !Writable())
        {
            volume.Defer(onDone, Unwritable());
            return;
        }

        writeData = data;
        this->onDone = onDone;
        Claim(Call::write);
    }

    void FatLiteFile::Truncate(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !Writable())
        {
            volume.Defer(onDone, Unwritable());
            return;
        }

        this->onDone = onDone;
        Claim(Call::truncate);
    }

    void FatLiteFile::ExpectSize(uint32_t, const infra::Function<void(Result result)>& onDone)
    {
        if (Pending() || !Writable())
            volume.Defer(onDone, Unwritable());
        else
            volume.Defer(onDone, volume.Available(generation) ? Result::unsupported : volume.Unavailable());
    }

    void FatLiteFile::Sync(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending())
            volume.Defer(onDone, Result::busy);
        else if (!Writable())
            volume.Defer(onDone, volume.Available(generation) ? Result::success : volume.Unavailable());
        else
        {
            this->onDone = onDone;
            Claim(Call::sync);
        }
    }

    void FatLiteFile::Close(const infra::Function<void(Result result)>& onDone)
    {
        if (Pending())
            volume.Defer(onDone, Result::busy);
        else if (!Writable())
        {
            auto result = volume.Available(generation) ? Result::success : volume.Unavailable();
            Detach();
            volume.Defer(onDone, result);
        }
        else
        {
            this->onDone = onDone;
            Claim(Call::close);
        }
    }

    void FatLiteFile::UnReferenced()
    {
        if (open && Writable() && Uncommitted() && volume.Available(generation))
        {
            call = Call::implicitClose;
            claimer.Claim([this]
                {
                    Granted();
                });
            return;
        }

        Detach();
        Release();
    }

    bool FatLiteFile::Pending() const
    {
        return call != Call::none;
    }

    bool FatLiteFile::Writable() const
    {
        return mode != OpenMode::read;
    }

    Result FatLiteFile::Unwritable() const
    {
        return Pending() ? Result::busy : Result::readOnly;
    }

    void FatLiteFile::Claim(Call newCall)
    {
        call = newCall;
        self = Share(*this);
        claimer.Claim([this]
            {
                Granted();
            });
    }

    void FatLiteFile::Granted()
    {
        transferred = 0;

        if (!volume.Available(generation))
            Finish(volume.Unavailable());
        else if (call == Call::read)
            ReadStep();
        else if (call == Call::write)
        {
            if (mode == OpenMode::append)
                position = size;

            WriteStep();
        }
        else if (call == Call::truncate)
            StartTruncate();
        else
            StartSync();
    }

    void FatLiteFile::Reach(uint32_t index)
    {
        target = index;

        if (cluster == 0 || clusterIndex > index)
        {
            cluster = firstCluster;
            clusterIndex = 0;
        }

        if (cluster == 0)
            Grow();
        else if (clusterIndex == index)
            Reached();
        else
            volume.Table().Next(cluster, [this](Result result, std::optional<uint32_t> next)
                {
                    Followed(result, next);
                });
    }

    void FatLiteFile::Followed(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success)
            Done(result);
        else if (next.has_value())
        {
            cluster = *next;
            ++clusterIndex;
            Reach(target);
        }
        else if (call == Call::write && clusterIndex + 1 == target && uint64_t{ target } * volume.ClusterBytes() >= size)
            Grow();
        else
            Done(Result::corrupted);
    }

    void FatLiteFile::Grow()
    {
        volume.Allocator().Take([this](Result result, uint32_t newCluster)
            {
                Taken(result, newCluster);
            });
    }

    void FatLiteFile::Taken(Result result, uint32_t newCluster)
    {
        taken = newCluster;

        if (result != Result::success)
            Done(result);
        else if (firstCluster == 0)
        {
            firstCluster = taken;
            Reach(target);
        }
        else if (volume.TableWriter().SectorOf(taken) != volume.TableWriter().SectorOf(cluster))
            volume.Window().Flush([this](Result result)
                {
                    if (result != Result::success)
                        Done(result);
                    else
                        Link();
                });
        else
            Link();
    }

    void FatLiteFile::Link()
    {
        volume.TableWriter().Write(cluster, taken, [this](Result result)
            {
                Linked(result);
            });
    }

    void FatLiteFile::Linked(Result result)
    {
        if (result != Result::success)
            Done(result);
        else
        {
            cluster = taken;
            ++clusterIndex;
            Reach(target);
        }
    }

    void FatLiteFile::Reached()
    {
        auto sectorSize = volume.Window().SectorSize();
        auto sectorsLeft = volume.Geometry().sectorsPerCluster - SectorInCluster();

        if (call == Call::read)
        {
            auto remaining = std::min<std::size_t>(readBuffer.size() - transferred, size - position);

            if (position % sectorSize != 0 || remaining < sectorSize)
                ReadPart();
            else
                ReadSectors(static_cast<uint32_t>(std::min<std::size_t>(remaining / sectorSize, sectorsLeft)));
        }
        else if (call == Call::write)
        {
            auto remaining = std::min<std::size_t>(writeData.size() - transferred, maxSize - position);

            if (position % sectorSize != 0 || remaining < sectorSize)
                WritePart(remaining);
            else
                WriteSectors(static_cast<uint32_t>(std::min<std::size_t>(remaining / sectorSize, sectorsLeft)));
        }
        else
            volume.Table().Next(cluster, [this](Result result, std::optional<uint32_t> next)
                {
                    Cut(result, next);
                });
    }

    void FatLiteFile::ReadStep()
    {
        if (std::min<std::size_t>(readBuffer.size() - transferred, size - position) == 0)
            Done(Result::success);
        else if (position / volume.ClusterBytes() >= volume.Geometry().clusterCount)
            Done(Result::corrupted);
        else
            Reach(position / volume.ClusterBytes());
    }

    void FatLiteFile::ReadPart()
    {
        volume.Window().Read(Sector(), [this](Result result, infra::ConstByteRange contents)
            {
                auto offset = position % contents.size();
                moving = std::min<std::size_t>({ contents.size() - offset, readBuffer.size() - transferred, size - position });

                if (result == Result::success)
                    infra::Copy(infra::Head(infra::DiscardHead(contents, offset), moving), infra::Head(infra::DiscardHead(readBuffer, transferred), moving));

                Moved(result);
            });
    }

    void FatLiteFile::ReadSectors(uint32_t sectors)
    {
        transferSector = Sector();
        moving = std::size_t{ sectors } * volume.Window().SectorSize();
        volume.Device().ReadBlocks(infra::Head(infra::DiscardHead(readBuffer, transferred), moving), transferSector, [this](hal::BlockDevice::Result result)
            {
                if (result == hal::BlockDevice::Result::success)
                    volume.Window().Overlay(transferSector, infra::Head(infra::DiscardHead(readBuffer, transferred), moving));

                Moved(ToResult(result));
            });
    }

    void FatLiteFile::WriteStep()
    {
        if (transferred == writeData.size())
            Done(Result::success);
        else if (position == maxSize || position / volume.ClusterBytes() >= volume.Geometry().clusterCount)
            Done(Result::noSpace);
        else
            Reach(position / volume.ClusterBytes());
    }

    void FatLiteFile::WritePart(std::size_t remaining)
    {
        auto offset = position % volume.Window().SectorSize();
        moving = std::min<std::size_t>(volume.Window().SectorSize() - offset, remaining);
        auto onSector = [this](Result result, infra::ByteRange contents)
        {
            if (result == Result::success)
                infra::Copy(infra::Head(infra::DiscardHead(writeData, transferred), moving), infra::Head(infra::DiscardHead(contents, position % contents.size()), moving));

            Moved(result);
        };

        if (position - offset >= size)
            volume.Window().Overwrite(Sector(), dataRank, onSector);
        else
            volume.Window().Modify(Sector(), dataRank, onSector);
    }

    void FatLiteFile::WriteSectors(uint32_t sectors)
    {
        transferSector = Sector();
        moving = std::size_t{ sectors } * volume.Window().SectorSize();
        volume.Window().Drop(transferSector, sectors);
        volume.Barrier().Write(infra::Head(infra::DiscardHead(writeData, transferred), moving), transferSector, dataRank, [this](hal::BlockDevice::Result result)
            {
                Moved(ToResult(result));
            });
    }

    void FatLiteFile::Moved(Result result)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        position += static_cast<uint32_t>(moving);
        transferred += moving;

        if (call == Call::read)
            ReadStep();
        else
        {
            size = std::max(size, position);
            written = true;
            WriteStep();
        }
    }

    void FatLiteFile::StartTruncate()
    {
        if (position == size && (size != 0 || firstCluster == 0))
        {
            Done(Result::success);
            return;
        }

        auto keep = static_cast<uint32_t>((uint64_t{ position } + volume.ClusterBytes() - 1) / volume.ClusterBytes());

        if (keep != 0)
            Reach(keep - 1);
        else
        {
            size = 0;
            written = true;
            cut = firstCluster;
            firstCluster = 0;
            cluster = 0;
            clusterIndex = 0;
            Commit([this](Result result)
                {
                    TruncationCommitted(result);
                });
        }
    }

    void FatLiteFile::Cut(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        size = position;
        written = true;
        cut = next.value_or(0);
        Commit([this](Result result)
            {
                TruncationCommitted(result);
            });
    }

    void FatLiteFile::TruncationCommitted(Result result)
    {
        if (result != Result::success)
            Done(result);
        else
            volume.Window().Flush([this](Result result)
                {
                    EndChain(result);
                });
    }

    void FatLiteFile::EndChain(Result result)
    {
        if (result != Result::success || cut == 0)
            Done(result);
        else if (firstCluster == 0)
            FreeCut(Result::success);
        else
            volume.TableWriter().Write(cluster, EndOfChain(volume.Geometry().type), [this](Result result)
                {
                    if (result != Result::success)
                        Done(result);
                    else
                        volume.Window().Flush([this](Result result)
                            {
                                FreeCut(result);
                            });
                });
    }

    void FatLiteFile::FreeCut(Result result)
    {
        if (result != Result::success)
            Done(result);
        else
            volume.Allocator().Free(cut, [this](Result result)
                {
                    Flush(result);
                });
    }

    void FatLiteFile::StartSync()
    {
        if (Uncommitted())
            Commit([this](Result result)
                {
                    Flush(result);
                });
        else
            Flush(Result::success);
    }

    void FatLiteFile::Flush(Result result)
    {
        if (result != Result::success)
            Done(result);
        else
            volume.Window().Flush([this](Result result)
                {
                    Done(result);
                });
    }

    void FatLiteFile::Done(Result result)
    {
        volume.Report(result);
        Finish(result);
    }

    void FatLiteFile::Finish(Result result)
    {
        auto completed = std::exchange(call, Call::none);
        claimer.Release();

        if (completed == Call::close || completed == Call::implicitClose)
            Detach();

        if (completed == Call::implicitClose)
        {
            Release();
            return;
        }

        self = nullptr;

        if (completed == Call::read)
            onRead(result, transferred);
        else
            onDone(result);
    }

    uint32_t FatLiteFile::SectorInCluster() const
    {
        return position % volume.ClusterBytes() / volume.Window().SectorSize();
    }

    uint32_t FatLiteFile::Sector() const
    {
        return volume.Geometry().SectorOfCluster(cluster) + SectorInCluster();
    }
}
