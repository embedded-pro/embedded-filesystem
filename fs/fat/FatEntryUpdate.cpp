#include "fs/fat/FatEntryUpdate.hpp"
#include "fs/fat/FatRanks.hpp"
#include <algorithm>

namespace fs
{
    FatEntryUpdate::FatEntryUpdate(SectorCache& cache, FatDirectoryReader& reader)
        : cache(cache)
        , reader(reader)
    {}

    FatEntryUpdate::~FatEntryUpdate() = default;

    void FatEntryUpdate::Commit(FatOpenFile& file, const infra::Function<void(Result result)>& onDone)
    {
        this->file = &file;
        this->onDone = onDone;
        cache.Read(reader.SectorOf(file.entry), [this](Result result, infra::ConstByteRange contents)
            {
                OnSector(result, contents);
            });
    }

    void FatEntryUpdate::OnSector(Result result, infra::ConstByteRange contents)
    {
        if (result != Result::success)
        {
            Done(result);
            return;
        }

        auto current = infra::Head(infra::DiscardHead(contents, Offset()), directoryEntryBytes);
        auto entry = DecodeShortEntry(current);
        entry.size = file->size;
        entry.firstCluster = file->firstCluster;

        if (file->written)
            entry.attributes |= FatAttributes::archive;

        EncodeShortEntry(entry, infra::MakeRange(updated));

        if (std::equal(updated.begin(), updated.end(), current.begin()))
            Done(Result::success);
        else
            cache.Modify(reader.SectorOf(file->entry), entryRank, [this](Result result, infra::ByteRange contents)
                {
                    if (result == Result::success)
                        infra::Copy(infra::MakeRange(updated), infra::Head(infra::DiscardHead(contents, Offset()), directoryEntryBytes));

                    Done(result);
                });
    }

    void FatEntryUpdate::Done(Result result)
    {
        if (result == Result::success)
            file->Committed();

        onDone(result);
    }

    uint32_t FatEntryUpdate::Offset() const
    {
        return file->entry.index % (cache.SectorSize() / directoryEntryBytes) * directoryEntryBytes;
    }
}
