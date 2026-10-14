#include "fs/nor/NorDirectoryRead.hpp"

namespace fs
{
    NorDirectoryRead::NorDirectoryRead(NorInodeRead& inodeRead, NorEntrySeek& entrySeek)
        : inodeRead(inodeRead)
        , entrySeek(entrySeek)
    {}

    void NorDirectoryRead::Read(const NorMapRoot& inodeTable, uint32_t directory, uint32_t fromKey, const infra::Function<void(Result result)>& onRead)
    {
        this->inodeTable = inodeTable;
        this->directory = directory;
        this->fromKey = fromKey;
        onDone = onRead;
        inodeRead.Read(inodeTable, directory, [this](Result result)
            {
                DirectoryRead(result);
            });
    }

    const std::optional<NorEntrySeek::Entry>& NorDirectoryRead::Found() const
    {
        return entrySeek.Found();
    }

    EntryInfo NorDirectoryRead::Info() const
    {
        const auto& record = inodeRead.Record();
        if (record.type == NorInodeType::directory)
            return EntryInfo{ infra::ByteRangeAsString(record.name), EntryType::directory, 0 };

        return EntryInfo{ infra::ByteRangeAsString(record.name), EntryType::file, record.size };
    }

    void NorDirectoryRead::DirectoryRead(Result result)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        const auto& record = inodeRead.Record();
        entrySeek.Seek(directory, NorMapRoot{ record.mapRoot, record.mapHeight }, fromKey, [this](Result sought)
            {
                Sought(sought);
            });
    }

    void NorDirectoryRead::Sought(Result result)
    {
        const auto& found = entrySeek.Found();
        if (result != Result::success || !found.has_value())
            onDone(result);
        else
            inodeRead.Read(inodeTable, found->inode, [this](Result read)
                {
                    EntryRead(read);
                });
    }

    void NorDirectoryRead::EntryRead(Result result)
    {
        if (result == Result::success && inodeRead.Record().parent != directory)
            onDone(Result::corrupted);
        else
            onDone(result);
    }
}
