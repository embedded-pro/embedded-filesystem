#include "fs/nor/NorFileTruncate.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t blockNumberBytes = 4;
        constexpr uint32_t superblocks = 2;
    }

    NorFileTruncate::NorFileTruncate(NorVolume& volume)
        : volume(volume)
    {}

    NorFileTruncate::~NorFileTruncate() = default;

    void NorFileTruncate::Truncate(NorOpenFile& truncated, uint32_t truncatedSize, const infra::Function<void(Result result)>& onTruncated)
    {
        file = &truncated;
        size = truncatedSize;
        onDone = onTruncated;

        if (!file->entries.empty() && size != 0 && size < file->programmed)
            volume.FileCommit().Commit(*file, [this](Result result)
                {
                    if (result == Result::success)
                        TruncateCommitted();
                    else
                        onDone(result);
                });
        else
            TruncateCommitted();
    }

    void NorFileTruncate::TruncateCommitted()
    {
        auto payload = volume.Geometry().PayloadSize();
        FreeEntriesFrom(static_cast<uint32_t>((uint64_t{ size } + payload - 1) / payload));

        if (size >= file->programmed)
        {
            file->size = size;
            Commit();
        }
        else if (size == 0)
        {
            file->size = 0;
            file->programmed = 0;
            file->tailBlock = std::nullopt;
            file->ownsTailBlock = false;
            Commit();
        }
        else
        {
            lastIndex = (size - 1) / payload;
            FindTail();
        }
    }

    void NorFileTruncate::FreeEntriesFrom(uint32_t count) const
    {
        auto& entries = file->entries;
        for (auto index = entries.size(); index != 0; --index)
            if (entries[index - 1].index >= count)
            {
                volume.State().blocks.Set(entries[index - 1].block, NorBlockState::freeNotErased);
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index - 1));
            }
    }

    void NorFileTruncate::FindTail()
    {
        volume.Cache().UnpinAll();
        volume.Fetch().Fetch(InodeTable(), volume.State().inodeTable, file->inode, [this](NorFetchResult result)
            {
                auto record = result == NorFetchResult::fetched ? NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, file->inode) : std::nullopt;
                if (!record.has_value())
                {
                    volume.Cache().UnpinAll();
                    onDone(Result::corrupted);
                    return;
                }

                dataMap = NorMapRoot{ LoadLittle32(*record, norInodeMapRootOffset), (*record)[norInodeMapHeightOffset] };
                volume.Cache().UnpinAll();
                volume.Fetch().Fetch(DataMap(), dataMap, lastIndex, [this](NorFetchResult fetched)
                    {
                        TailFetched(fetched);
                    });
            });
    }

    void NorFileTruncate::TailFetched(NorFetchResult result)
    {
        auto block = result == NorFetchResult::fetched ? NorRadixTree(volume.Cache(), file->inode).Find(dataMap, lastIndex) : 0;
        volume.Cache().UnpinAll();

        if (!IsDataBlock(block))
            onDone(Result::corrupted);
        else
            TailFound(block);
    }

    void NorFileTruncate::TailFound(uint32_t block)
    {
        const auto& geometry = volume.Geometry();
        auto start = lastIndex * geometry.PayloadSize();
        auto inlineLength = (size - start) % geometry.programUnit;

        file->size = size;
        file->programmed = size - inlineLength;
        file->tailBlock = block;
        file->ownsTailBlock = false;

        if (inlineLength == 0)
            Commit();
        else
            volume.Flash().ReadBuffer(infra::Head(file->buffer, inlineLength), geometry.AddressOfBlock(block) + geometry.HeaderSize() + file->programmed - start, [this]
                {
                    Commit();
                });
    }

    void NorFileTruncate::Commit()
    {
        volume.FileCommit().Commit(*file, [this](Result result)
            {
                onDone(result);
            });
    }

    bool NorFileTruncate::IsDataBlock(uint32_t block) const
    {
        return block >= superblocks && block < volume.Geometry().blocks && volume.State().blocks.State(block) == NorBlockState::inUse;
    }

    NorMapId NorFileTruncate::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorFileTruncate::DataMap() const
    {
        return NorMapId{ NorMap::fileData, file->inode, blockNumberBytes };
    }
}
