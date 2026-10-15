#include "fs/nor/NorFileOverwrite.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t blockNumberBytes = 4;
        constexpr uint32_t superblocks = 2;
    }

    NorFileOverwrite::NorFileOverwrite(NorVolume& volume)
        : volume(volume)
    {}

    NorFileOverwrite::~NorFileOverwrite() = default;

    void NorFileOverwrite::Overwrite(NorOpenFile& overwritten, infra::ConstByteRange bytes, const infra::Function<void(Result result)>& onOverwritten)
    {
        file = &overwritten;
        data = bytes;
        onDone = onOverwritten;
        Next();
    }

    void NorFileOverwrite::Next()
    {
        auto more = true;
        while (more)
            more = Step();
    }

    bool NorFileOverwrite::Step()
    {
        if (data.empty())
        {
            onDone(Result::success);
            return false;
        }

        if (file->position >= file->programmed)
        {
            Buffer(SegmentEnd());
            return true;
        }

        index = file->position / volume.Geometry().PayloadSize();
        CopyBlock();
        return false;
    }

    void NorFileOverwrite::CopyBlock()
    {
        auto uncommitted = file->UncommittedBlock(index);
        if (uncommitted.has_value())
            SourceFound(uncommitted.value_or(0), true);
        else
            FindSource();
    }

    void NorFileOverwrite::FindSource()
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
                volume.Fetch().Fetch(DataMap(), dataMap, index, [this](NorFetchResult fetched)
                    {
                        SourceFetched(fetched);
                    });
            });
    }

    void NorFileOverwrite::SourceFetched(NorFetchResult result)
    {
        auto block = result == NorFetchResult::fetched ? NorRadixTree(volume.Cache(), file->inode).Find(dataMap, index) : 0;
        volume.Cache().UnpinAll();

        if (!IsDataBlock(block))
            onDone(Result::corrupted);
        else
            SourceFound(block, false);
    }

    void NorFileOverwrite::SourceFound(uint32_t block, bool uncommitted)
    {
        source = block;
        sourceUncommitted = uncommitted;

        if (!sourceUncommitted && !file->Admits(index, volume.Geometry().PayloadSize(), NorRadixFanout(volume.Geometry().nodeSize)))
            volume.FileCommit().Commit(*file, [this](Result result)
                {
                    if (result == Result::success)
                        CopyBlock();
                    else
                        onDone(result);
                });
        else
            volume.Allocator().Allocate(NorBlockType::data, NorBlockOwner{ file->inode, index }, [this](std::optional<uint32_t> allocated)
                {
                    Allocated(allocated);
                });
    }

    void NorFileOverwrite::Allocated(std::optional<uint32_t> block)
    {
        if (!block.has_value())
        {
            onDone(Result::noSpace);
            return;
        }

        copy = block.value_or(0);
        copied = 0;
        length = std::min(file->programmed, BlockStart() + volume.Geometry().PayloadSize()) - BlockStart();
        CopyNextChunk();
    }

    void NorFileOverwrite::CopyNextChunk()
    {
        if (copied == length)
        {
            BlockCopied();
            return;
        }

        const auto& geometry = volume.Geometry();
        auto buffer = volume.JournalBuffer();
        chunk = std::min(static_cast<uint32_t>(buffer.size()), length - copied);
        volume.Flash().ReadBuffer(infra::Head(buffer, chunk), geometry.AddressOfBlock(source) + geometry.HeaderSize() + copied, [this]
            {
                const auto& programGeometry = volume.Geometry();
                auto chunkBytes = infra::Head(volume.JournalBuffer(), chunk);
                Merge(chunkBytes);
                volume.Flash().WriteBuffer(chunkBytes, programGeometry.AddressOfBlock(copy) + programGeometry.HeaderSize() + copied, [this]
                    {
                        copied += chunk;
                        CopyNextChunk();
                    });
            });
    }

    void NorFileOverwrite::Merge(infra::ByteRange chunkBytes) const
    {
        auto chunkStart = BlockStart() + copied;
        auto from = std::max(file->position, chunkStart);
        auto to = std::min({ SegmentEnd(), file->programmed, chunkStart + chunk });
        if (from < to)
            std::copy(data.begin() + (from - file->position), data.begin() + (to - file->position), chunkBytes.begin() + (from - chunkStart));
    }

    void NorFileOverwrite::BlockCopied()
    {
        if (sourceUncommitted)
        {
            volume.State().blocks.Set(source, NorBlockState::freeNotErased);
            for (auto& entry : file->entries)
                if (entry.index == index)
                    entry.block = copy;
        }
        else
            file->entries.push_back(NorBlockEntry{ index, source, copy });

        if (file->tailBlock == source)
        {
            file->tailBlock = copy;
            file->ownsTailBlock = true;
        }

        auto end = SegmentEnd();
        if (end > file->programmed)
        {
            Advance(file->programmed);
            Buffer(end);
        }
        else
            Advance(end);

        Next();
    }

    void NorFileOverwrite::Buffer(uint32_t to)
    {
        std::copy(data.begin(), data.begin() + (to - file->position), file->buffer.begin() + (file->position - file->programmed));
        Advance(to);
    }

    void NorFileOverwrite::Advance(uint32_t to)
    {
        data = infra::DiscardHead(data, to - file->position);
        file->position = to;
    }

    uint32_t NorFileOverwrite::BlockStart() const
    {
        return index * volume.Geometry().PayloadSize();
    }

    uint32_t NorFileOverwrite::SegmentEnd() const
    {
        auto payload = volume.Geometry().PayloadSize();
        auto blockEnd = (file->position / payload + 1) * payload;
        return std::min(file->position + static_cast<uint32_t>(data.size()), blockEnd);
    }

    bool NorFileOverwrite::IsDataBlock(uint32_t block) const
    {
        return block >= superblocks && block < volume.Geometry().blocks && volume.State().blocks.State(block) == NorBlockState::inUse;
    }

    NorMapId NorFileOverwrite::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorFileOverwrite::DataMap() const
    {
        return NorMapId{ NorMap::fileData, file->inode, blockNumberBytes };
    }
}
