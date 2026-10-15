#include "fs/nor/NorFileFlush.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    NorFileFlush::NorFileFlush(NorVolume& volume)
        : volume(volume)
    {}

    NorFileFlush::~NorFileFlush() = default;

    void NorFileFlush::Flush(NorOpenFile& flushed, uint32_t flushTo, NorReserve reserve, const infra::Function<void(Result result)>& onFlushed)
    {
        really_assert(flushed.tailBlock.has_value() && flushTo > flushed.programmed && flushTo <= flushed.size);

        file = &flushed;
        to = flushTo;
        onDone = onFlushed;

        if (file->ownsTailBlock)
            Program();
        else
            volume.Allocator().Allocate(NorBlockType::data, NorBlockOwner{ file->inode, Index() }, reserve, [this](std::optional<uint32_t> block)
                {
                    Copied(block);
                });
    }

    void NorFileFlush::Copied(std::optional<uint32_t> block)
    {
        if (!block.has_value())
        {
            onDone(Result::noSpace);
            return;
        }

        file->entries.push_back(NorBlockEntry{ Index(), file->tailBlock.value_or(0), block.value_or(0) });
        copy = block.value_or(0);
        copied = 0;
        CopyNextChunk();
    }

    void NorFileFlush::CopyNextChunk()
    {
        const auto& geometry = volume.Geometry();
        auto length = OffsetInBlock(file->programmed);
        if (copied == length)
        {
            file->tailBlock = copy;
            file->ownsTailBlock = true;
            Program();
            return;
        }

        auto buffer = volume.JournalBuffer();
        chunk = std::min(static_cast<uint32_t>(buffer.size()), length - copied);
        volume.Flash().ReadBuffer(infra::Head(buffer, chunk), geometry.AddressOfBlock(file->tailBlock.value_or(0)) + geometry.HeaderSize() + copied, [this]
            {
                const auto& programGeometry = volume.Geometry();
                volume.Flash().WriteBuffer(infra::Head(volume.JournalBuffer(), chunk), programGeometry.AddressOfBlock(copy) + programGeometry.HeaderSize() + copied, [this]
                    {
                        copied += chunk;
                        CopyNextChunk();
                    });
            });
    }

    void NorFileFlush::Program()
    {
        const auto& geometry = volume.Geometry();
        volume.Flash().WriteBuffer(infra::Head(file->buffer, to - file->programmed), geometry.AddressOfBlock(file->tailBlock.value_or(0)) + geometry.HeaderSize() + OffsetInBlock(file->programmed), [this]
            {
                auto pending = file->Pending();
                std::copy(pending.begin() + (to - file->programmed), pending.end(), pending.begin());
                file->programmed = to;
                onDone(Result::success);
            });
    }

    uint32_t NorFileFlush::Index() const
    {
        return file->programmed / volume.Geometry().PayloadSize();
    }

    uint32_t NorFileFlush::OffsetInBlock(uint32_t offset) const
    {
        return offset - Index() * volume.Geometry().PayloadSize();
    }
}
