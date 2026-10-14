#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <optional>
#include <utility>

namespace fs
{
    NorImageBuilder::NorImageBuilder(NorImage& image, const NorConfig& config)
        : image(image)
        , config(config)
        , geometry(*MakeNorGeometry(config, image.SectorSize(), image.Sectors()))
    {
        FindCheckpoint();
    }

    const NorGeometry& NorImageBuilder::Geometry() const
    {
        return geometry;
    }

    uint32_t NorImageBuilder::CheckpointBlock() const
    {
        return checkpointBlock;
    }

    uint32_t NorImageBuilder::JournalBlock() const
    {
        return journalBlock;
    }

    uint32_t NorImageBuilder::JournalEnd() const
    {
        return journalEnd;
    }

    uint64_t NorImageBuilder::NextSequence() const
    {
        return nextSequence;
    }

    NorCheckpoint NorImageBuilder::Checkpoint() const
    {
        auto bytes = infra::Head(infra::MakeRange(buffer), geometry.CheckpointBytes());
        Read(geometry.AddressOfBlock(checkpointBlock) + geometry.HeaderSize(), bytes);
        return *OpenNorCheckpoint(bytes);
    }

    void NorImageBuilder::SetEraseCount(uint32_t block, uint32_t count)
    {
        EraseBlock(block);
        auto partA = Scratch(geometry.RoundToProgramUnit(norHeaderPartABytes));
        SerializeNorHeaderA(count, partA);
        Program(geometry.AddressOfBlock(block), partA);
    }

    void NorImageBuilder::EraseBlock(uint32_t block)
    {
        for (auto sector = geometry.FirstSectorOfBlock(block); sector != geometry.FirstSectorOfBlock(block + 1); ++sector)
            image.EraseSector(sector);
    }

    uint64_t NorImageBuilder::Allocate(uint32_t block, NorBlockType type, NorBlockOwner owner)
    {
        auto partB = Scratch(geometry.RoundToProgramUnit(norHeaderPartBBytes));
        SerializeNorHeaderB(NorAllocation{ type, nextSequence, owner }, partB);
        Program(geometry.AddressOfBlock(block) + geometry.HeaderPartBOffset(), partB);
        return nextSequence++;
    }

    void NorImageBuilder::WriteCheckpoint(uint32_t block)
    {
        auto bytes = Scratch(geometry.RoundToProgramUnit(geometry.CheckpointBytes()));
        Read(geometry.AddressOfBlock(checkpointBlock) + geometry.HeaderSize(), infra::Head(bytes, geometry.CheckpointBytes()));
        auto checkpoint = *OpenNorCheckpoint(bytes);
        checkpoint.recordsBlock = block;
        checkpoint.recordsOffset = geometry.HeaderSize() + static_cast<uint32_t>(bytes.size());

        auto rewritten = infra::Head(infra::DiscardHead(infra::MakeRange(buffer), bytes.size()), bytes.size());
        std::ranges::fill(rewritten, uint8_t{ 0xff });
        SerializeNorCheckpoint(checkpoint, rewritten);
        std::ranges::copy(infra::Head(infra::DiscardHead(bytes, norCheckpointFixedBytes), NorCheckpointBitmapBytes(geometry.blocks)), rewritten.begin() + norCheckpointFixedBytes);
        SetInUse(rewritten, checkpointBlock, false);
        for (auto journal : epochJournalBlocks)
            SetInUse(rewritten, journal, false);
        SetInUse(rewritten, block, true);

        Allocate(block, NorBlockType::checkpoint);
        ProgramCheckpoint(block, rewritten);
        checkpointBlock = block;
        journalBlock = block;
        journalEnd = checkpoint.recordsOffset;
        epochJournalBlocks.clear();
        linkPending = false;
    }

    void NorImageBuilder::WriteCheckpoint(uint32_t block, const NorCheckpoint& checkpoint, std::initializer_list<uint32_t> used)
    {
        auto bytes = Scratch(geometry.RoundToProgramUnit(NorCheckpointBytes(checkpoint.blocks)));
        SerializeNorCheckpoint(checkpoint, bytes);
        for (auto inUse : used)
            MarkNorBlockInUse(bytes, inUse);
        ProgramCheckpoint(block, bytes);
    }

    void NorImageBuilder::AppendRecord(std::initializer_list<NorEntry> entries)
    {
        CrcRecord record{ Scratch(static_cast<uint32_t>(buffer.size())) };
        if (std::exchange(linkPending, false))
            AddNorEntry(record, link);
        for (const auto& entry : entries)
            AddNorEntry(record, entry);

        journalEnd = ProgramRecord(journalBlock, journalEnd, record);
    }

    uint32_t NorImageBuilder::AppendRecord(uint32_t block, uint32_t offset, std::initializer_list<NorEntry> entries)
    {
        CrcRecord record{ Scratch(static_cast<uint32_t>(buffer.size())) };
        for (const auto& entry : entries)
            AddNorEntry(record, entry);

        return ProgramRecord(block, offset, record);
    }

    void NorImageBuilder::StartJournalBlock(uint32_t block)
    {
        Allocate(block, NorBlockType::journal);
        link = NorLink{ journalBlock, journalEnd };
        linkPending = true;
        journalBlock = block;
        journalEnd = geometry.HeaderSize();
        epochJournalBlocks.push_back(block);
    }

    void NorImageBuilder::Program(uint32_t address, infra::ConstByteRange bytes)
    {
        image.Program(address, bytes);
    }

    void NorImageBuilder::Read(uint32_t address, infra::ByteRange bytes) const
    {
        image.Read(address, bytes);
    }

    void NorImageBuilder::Corrupt(uint32_t address)
    {
        std::array<uint8_t, 1> byte{};
        Read(address, infra::MakeRange(byte));
        really_assert(byte[0] != 0);
        Program(address, infra::MakeRange(std::array<uint8_t, 1>{ 0 }));
    }

    void NorImageBuilder::FindCheckpoint()
    {
        std::optional<uint64_t> checkpointSequence;
        auto header = infra::Head(infra::MakeRange(buffer), geometry.HeaderSize());

        for (uint32_t block = 2; block != geometry.blocks; ++block)
        {
            Read(geometry.AddressOfBlock(block), header);
            auto allocation = ParseNorBlockHeader(header, geometry.programUnit).allocation;
            if (!allocation.has_value())
                continue;

            nextSequence = std::max(nextSequence, allocation->sequence + 1);
            if (allocation->type == NorBlockType::checkpoint && (!checkpointSequence.has_value() || allocation->sequence > *checkpointSequence))
            {
                checkpointSequence = allocation->sequence;
                checkpointBlock = block;
            }
        }

        journalBlock = checkpointBlock;
        journalEnd = Checkpoint().recordsOffset;
    }

    infra::ByteRange NorImageBuilder::Scratch(uint32_t size)
    {
        really_assert(size <= buffer.size());

        auto bytes = infra::Head(infra::MakeRange(buffer), size);
        std::ranges::fill(bytes, uint8_t{ 0xff });
        return bytes;
    }

    uint32_t NorImageBuilder::ProgramRecord(uint32_t block, uint32_t offset, const CrcRecord& record)
    {
        auto rounded = infra::Head(infra::MakeRange(buffer), geometry.RoundToProgramUnit(static_cast<uint32_t>(record.Seal().size())));
        really_assert(offset + rounded.size() <= geometry.BlockSize());
        Program(geometry.AddressOfBlock(block) + offset, rounded);
        return offset + static_cast<uint32_t>(rounded.size());
    }

    void NorImageBuilder::ProgramCheckpoint(uint32_t block, infra::ByteRange bytes)
    {
        SealNorCheckpoint(bytes);
        Program(geometry.AddressOfBlock(block) + geometry.HeaderSize(), bytes);
    }

    void NorImageBuilder::SetInUse(infra::ByteRange checkpoint, uint32_t block, bool inUse) const
    {
        auto& byte = checkpoint[norCheckpointFixedBytes + block / 8];
        auto bit = static_cast<uint8_t>(1u << (block % 8));
        byte = inUse ? static_cast<uint8_t>(byte | bit) : static_cast<uint8_t>(byte & ~bit);
    }
}
