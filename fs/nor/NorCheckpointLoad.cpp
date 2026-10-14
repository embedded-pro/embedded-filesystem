#include "fs/nor/NorCheckpointLoad.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorLayout.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t blocksOffset = 24;
        constexpr uint32_t bitsPerByte = 8;
        constexpr uint32_t rootInode = 1;
    }

    NorCheckpointLoad::NorCheckpointLoad(hal::Flash& flash, NorVolumeState& volume, infra::ByteRange buffer, const NorScanSummary& summary, const infra::Function<void(Result result)>& onDone)
        : flash(flash)
        , volume(volume)
        , buffer(buffer)
        , averageCount(summary.AverageCount())
        , candidate(summary.checkpointCandidate.value_or(0))
        , candidateSequence(summary.checkpointSequence)
        , onDone(onDone)
    {}

    void NorCheckpointLoad::Start()
    {
        if (candidate == 0)
            onDone(Result::corrupted);
        else
            Verify(candidate, candidateSequence);
    }

    void NorCheckpointLoad::Verify(uint32_t block, uint64_t sequence)
    {
        candidate = block;
        candidateSequence = sequence;
        Read(0, norCheckpointFixedBytes, [this]
            {
                OnFixedPart();
            });
    }

    void NorCheckpointLoad::OnFixedPart()
    {
        auto fixedPart = infra::Head(buffer, norCheckpointFixedBytes);
        uint64_t bitmapBytes = (uint64_t{ LoadLittle32(fixedPart, blocksOffset) } + bitsPerByte - 1) / bitsPerByte;
        if (norCheckpointFixedBytes + bitmapBytes + norCheckpointCrcBytes > volume.geometry->PayloadSize())
        {
            FindPreviousCheckpoint();
            return;
        }

        loaded = ParseNorCheckpointFixedPart(fixedPart);
        crcOffset = norCheckpointFixedBytes + static_cast<uint32_t>(bitmapBytes);
        crc = infra::Crc32{};
        crc.Update(fixedPart);
        position = norCheckpointFixedBytes;
        ReadForCrc();
    }

    void NorCheckpointLoad::ReadForCrc()
    {
        if (position == crcOffset)
        {
            Read(crcOffset, norCheckpointCrcBytes, [this]
                {
                    OnStoredCrc();
                });
            return;
        }

        auto size = static_cast<uint32_t>(std::min<std::size_t>(buffer.size(), crcOffset - position));
        Read(position, size, [this, size]
            {
                crc.Update(infra::Head(buffer, size));
                position += size;
                ReadForCrc();
            });
    }

    void NorCheckpointLoad::OnStoredCrc()
    {
        if (LoadLittle32(buffer, 0) != crc.Result())
            FindPreviousCheckpoint();
        else if (loaded.blocks != volume.geometry->blocks)
            onDone(Result::corrupted);
        else
            Accept();
    }

    void NorCheckpointLoad::FindPreviousCheckpoint()
    {
        previous = std::nullopt;
        position = superblocks;
        ReadNextCandidate();
    }

    void NorCheckpointLoad::ReadNextCandidate()
    {
        const auto& geometry = *volume.geometry;
        while (position != geometry.blocks && volume.blocks.State(position) != NorBlockState::pending)
            ++position;

        if (position == geometry.blocks && previous.has_value())
            Verify(*previous, previousSequence);
        else if (position == geometry.blocks)
            onDone(Result::corrupted);
        else
            flash.ReadBuffer(infra::Head(buffer, geometry.HeaderSize()), geometry.AddressOfBlock(position), [this]
                {
                    OnCandidateHeader();
                });
    }

    void NorCheckpointLoad::OnCandidateHeader()
    {
        const auto& geometry = *volume.geometry;
        auto allocation = ParseNorBlockHeader(infra::Head(buffer, geometry.HeaderSize()), geometry.programUnit).allocation.value_or(NorAllocation{});
        if (allocation.type == NorBlockType::checkpoint && allocation.sequence < candidateSequence && (!previous.has_value() || allocation.sequence > previousSequence))
        {
            previous = position;
            previousSequence = allocation.sequence;
        }

        ++position;
        ReadNextCandidate();
    }

    void NorCheckpointLoad::Accept()
    {
        volume.checkpoint = loaded;
        volume.checkpointBlock = candidate;
        position = 0;
        ReadBitmap();
    }

    void NorCheckpointLoad::ReadBitmap()
    {
        auto bitmapBytes = static_cast<uint32_t>(NorCheckpointBitmapBytes(volume.geometry->blocks));
        if (position == bitmapBytes)
        {
            onDone(Consistency());
            return;
        }

        auto size = static_cast<uint32_t>(std::min<std::size_t>(buffer.size(), bitmapBytes - position));
        Read(norCheckpointFixedBytes + position, size, [this, size]
            {
                OnBitmap(size);
            });
    }

    void NorCheckpointLoad::OnBitmap(uint32_t size)
    {
        auto blocks = volume.geometry->blocks;
        for (uint32_t index = 0; index != size; ++index)
            for (uint32_t bit = 0; bit != bitsPerByte && (position + index) * bitsPerByte + bit < blocks; ++bit)
                if (!Settle((position + index) * bitsPerByte + bit, ((buffer[index] >> bit) & 1) != 0))
                {
                    onDone(Result::corrupted);
                    return;
                }

        position += size;
        ReadBitmap();
    }

    bool NorCheckpointLoad::Settle(uint32_t block, bool inUse) const
    {
        auto state = volume.blocks.State(block);

        if (state == norUnknownEraseCount && inUse)
            return false;

        if (state == norUnknownEraseCount)
        {
            volume.eraseCounts.Record(block, averageCount);
            volume.blocks.Set(block, NorBlockState::freeNotErased);
        }
        else if (state == NorBlockState::freeErased && inUse)
            return false;
        else if (inUse)
            volume.blocks.Set(block, NorBlockState::inUse);

        return true;
    }

    Result NorCheckpointLoad::Consistency() const
    {
        const auto& checkpoint = volume.checkpoint;
        if (volume.blocks.State(volume.checkpointBlock) != NorBlockState::inUse || checkpoint.inodeTableHeight == 0 || checkpoint.inodeTableHeight > norMaxLevels || checkpoint.nextInode <= rootInode || !IsNodeInUse(checkpoint.inodeTableRoot) || !AreRecordsInUse())
            return Result::corrupted;

        return Result::success;
    }

    bool NorCheckpointLoad::IsNodeInUse(uint32_t address) const
    {
        const auto& geometry = *volume.geometry;
        auto block = address / geometry.BlockSize();
        auto offset = address % geometry.BlockSize();

        return block >= superblocks && block < geometry.blocks && offset >= geometry.NodeSlotsOffset() && (offset - geometry.NodeSlotsOffset()) % geometry.nodeSize == 0 && offset + geometry.nodeSize <= geometry.BlockSize() && volume.blocks.State(block) == NorBlockState::inUse;
    }

    bool NorCheckpointLoad::AreRecordsInUse() const
    {
        const auto& geometry = *volume.geometry;
        const auto& checkpoint = volume.checkpoint;
        auto lowest = checkpoint.recordsBlock == volume.checkpointBlock ? geometry.HeaderSize() + geometry.RoundToProgramUnit(geometry.CheckpointBytes()) : geometry.HeaderSize();

        return checkpoint.recordsBlock >= superblocks && checkpoint.recordsBlock < geometry.blocks && volume.blocks.State(checkpoint.recordsBlock) == NorBlockState::inUse && checkpoint.recordsOffset % geometry.programUnit == 0 && checkpoint.recordsOffset >= lowest && checkpoint.recordsOffset <= geometry.BlockSize();
    }

    void NorCheckpointLoad::Read(uint32_t offset, uint32_t size, const infra::Function<void()>& onReadDone)
    {
        const auto& geometry = *volume.geometry;
        onRead = onReadDone;
        flash.ReadBuffer(infra::Head(buffer, size), geometry.AddressOfBlock(candidate) + geometry.HeaderSize() + offset, [this]
            {
                onRead();
            });
    }
}
