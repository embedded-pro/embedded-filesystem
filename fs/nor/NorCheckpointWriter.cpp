#include "fs/nor/NorCheckpointWriter.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>
#include <utility>

namespace fs
{
    namespace
    {
        constexpr uint32_t bitsPerByte = 8;
    }

    NorCheckpointWriter::NorCheckpointWriter(NorVolume& volume)
        : volume(volume)
    {}

    NorCheckpointWriter::~NorCheckpointWriter() = default;

    void NorCheckpointWriter::Write(const infra::Function<void(Result result)>& onWritten)
    {
        onDone = onWritten;
        collect = volume.Collector().Due();
        WriteNextNode();
    }

    void NorCheckpointWriter::WriteNextNode()
    {
        const auto& state = volume.State();
        const auto& geometry = volume.Geometry();
        auto next = WriteBack().Next();

        if (!next.has_value() && std::exchange(collect, false))
            volume.Collector().Collect([this]
                {
                    WriteNextNode();
                });
        else if (!next.has_value())
            volume.Allocator().Allocate(NorBlockType::checkpoint, {}, NorReserve::used, [this](std::optional<uint32_t> allocated)
                {
                    CheckpointBlockAllocated(allocated);
                });
        else
        {
            slot = next.value_or(0);
            if (state.nodeBlock.has_value() && state.nodeOffset + geometry.nodeSize <= geometry.BlockSize())
                ProgramNode();
            else
                volume.Allocator().Allocate(NorBlockType::node, {}, NorReserve::used, [this](std::optional<uint32_t> allocated)
                    {
                        NodeBlockAllocated(allocated);
                    });
        }
    }

    void NorCheckpointWriter::NodeBlockAllocated(std::optional<uint32_t> allocated)
    {
        if (!allocated.has_value())
        {
            onDone(Result::noSpace);
            return;
        }

        auto& state = volume.State();
        state.blocks.Set(allocated.value_or(0), NorBlockState::inUse);
        state.nodeBlocks.Add(allocated.value_or(0));
        state.nodeBlock = allocated;
        state.nodeOffset = volume.Geometry().NodeSlotsOffset();
        ProgramNode();
    }

    void NorCheckpointWriter::ProgramNode()
    {
        auto& state = volume.State();
        auto address = volume.Geometry().AddressOfBlock(state.nodeBlock.value_or(0)) + state.nodeOffset;
        state.nodeOffset += volume.Geometry().nodeSize;
        volume.Flash().WriteBuffer(WriteBack().Seal(slot), address, [this, address]
            {
                auto& programmed = volume.State();
                WriteBack().Written(slot, address, programmed.inodeTable);
                ++programmed.liveNodes;
                WriteNextNode();
            });
    }

    void NorCheckpointWriter::CheckpointBlockAllocated(std::optional<uint32_t> allocated)
    {
        if (!allocated.has_value())
        {
            onDone(Result::noSpace);
            return;
        }

        auto& state = volume.State();
        const auto& geometry = volume.Geometry();
        block = allocated.value_or(0);
        state.blocks.Set(block, NorBlockState::inUse);
        checkpoint = NorCheckpoint{ state.inodeTable.address, state.inodeTable.height, state.nextInode, block, geometry.HeaderSize() + geometry.RoundToProgramUnit(geometry.CheckpointBytes()), state.liveNodes - volume.Cache().ReleasedNodes(), geometry.blocks };
        SerializeNorCheckpointFixedPart(checkpoint, infra::MakeRange(fixedPart));
        written = 0;
        crc.Reset();
        ProgramNextChunk();
    }

    void NorCheckpointWriter::ProgramNextChunk()
    {
        const auto& geometry = volume.Geometry();
        auto crcOffset = geometry.CheckpointBytes() - norCheckpointCrcBytes;
        auto lastChunk = crcOffset / geometry.programUnit * geometry.programUnit;
        auto total = geometry.RoundToProgramUnit(geometry.CheckpointBytes());
        auto buffer = volume.JournalBuffer();

        chunk = written < lastChunk ? std::min(static_cast<uint32_t>(buffer.size()), lastChunk - written) : total - written;
        auto bytes = infra::Head(buffer, chunk);
        std::ranges::fill(bytes, uint8_t{ 0xff });
        for (uint32_t index = 0; index != chunk && written + index < crcOffset; ++index)
        {
            bytes[index] = ByteAt(written + index);
            crc.Update(bytes[index]);
        }

        if (written + chunk == total)
            StoreLittle32(bytes, crcOffset - written, crc.Result());

        volume.Flash().WriteBuffer(bytes, geometry.AddressOfBlock(block) + geometry.HeaderSize() + written, [this, total]
            {
                written += chunk;
                if (written == total)
                    Commit();
                else
                    ProgramNextChunk();
            });
    }

    void NorCheckpointWriter::Commit()
    {
        auto& state = volume.State();
        state.blocks.Set(state.checkpointBlock, NorBlockState::freeNotErased);
        for (auto epochBlock : state.epoch)
            state.blocks.Set(epochBlock, NorBlockState::freeNotErased);

        volume.Collector().Committed();
        state.liveNodes = checkpoint.liveNodes;
        volume.Cache().ClearReleasedNodes();
        state.checkpoint = checkpoint;
        state.checkpointBlock = block;
        state.journal = NorJournal{ block, checkpoint.recordsOffset, 1 };
        state.epoch.clear();
        state.epoch.push_back(block);
        state.startNewJournalBlock = false;
        onDone(Result::success);
    }

    uint8_t NorCheckpointWriter::ByteAt(uint32_t offset) const
    {
        if (offset < norCheckpointFixedBytes)
            return fixedPart[offset];

        uint8_t byte = 0;
        auto first = (offset - norCheckpointFixedBytes) * bitsPerByte;
        for (uint32_t bit = 0; bit != bitsPerByte && first + bit < volume.Geometry().blocks; ++bit)
            if (IsInNewCheckpoint(first + bit))
                byte |= static_cast<uint8_t>(1u << bit);

        return byte;
    }

    bool NorCheckpointWriter::IsInNewCheckpoint(uint32_t candidate) const
    {
        const auto& state = volume.State();
        return state.blocks.State(candidate) == NorBlockState::inUse && candidate != state.checkpointBlock && std::ranges::find(state.epoch, candidate) == state.epoch.end();
    }

    NorNodeWriteBack NorCheckpointWriter::WriteBack() const
    {
        return NorNodeWriteBack{ volume.Cache(), NorInodeRecordBytes(volume.Config()) };
    }
}
