#include "fs/nor/NorJournalWriter.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    NorJournalWriter::NorJournalWriter(NorVolume& volume)
        : volume(volume)
    {}

    NorJournalWriter::~NorJournalWriter() = default;

    void NorJournalWriter::Commit(std::size_t entryBytes, const infra::Function<void(CrcRecord& record)>& add, const infra::Function<void(Result result)>& onCommitted)
    {
        really_assert(CrcRecord::overhead + norLinkEntryBytes + entryBytes <= volume.JournalBuffer().size());

        recordBytes = static_cast<uint32_t>(CrcRecord::overhead + entryBytes);
        addEntries = add;
        onDone = onCommitted;
        Place();
    }

    bool NorJournalWriter::IsMoreThanHalfFull() const
    {
        const auto& state = volume.State();
        const auto& geometry = volume.Geometry();
        uint64_t used = uint64_t{ state.journal.blocks - 1 } * geometry.PayloadSize() + state.journal.end - geometry.HeaderSize();
        return 2 * used > uint64_t{ volume.Config().journalBlocks } * geometry.PayloadSize();
    }

    void NorJournalWriter::Place()
    {
        const auto& state = volume.State();
        const auto& geometry = volume.Geometry();

        if (!state.startNewJournalBlock && state.journal.end + geometry.RoundToProgramUnit(recordBytes) <= geometry.BlockSize())
        {
            link = std::nullopt;
            Write();
        }
        else if (state.epoch.size() == volume.Config().journalBlocks)
            volume.Checkpoints().Write([this](Result result)
                {
                    if (result == Result::success)
                        Place();
                    else
                        onDone(result);
                });
        else
            volume.Allocator().Allocate(NorBlockType::journal, {}, NorReserve::used, [this](std::optional<uint32_t> block)
                {
                    BlockAllocated(block);
                });
    }

    void NorJournalWriter::BlockAllocated(std::optional<uint32_t> block)
    {
        if (!block.has_value())
        {
            onDone(Result::noSpace);
            return;
        }

        auto& state = volume.State();
        auto allocated = block.value_or(0);
        state.blocks.Set(allocated, NorBlockState::inUse);
        link = NorLink{ state.journal.block, state.journal.end };
        state.journal = NorJournal{ allocated, volume.Geometry().HeaderSize(), state.journal.blocks + 1 };
        state.epoch.push_back(allocated);
        state.startNewJournalBlock = false;
        Write();
    }

    void NorJournalWriter::Write()
    {
        const auto& state = volume.State();
        const auto& geometry = volume.Geometry();
        auto buffer = volume.JournalBuffer();
        std::ranges::fill(buffer, uint8_t{ 0xff });

        CrcRecord record{ buffer };
        if (link.has_value())
            AddNorEntry(record, *link);
        addEntries(record);
        sealed = record.Seal();

        volume.Flash().WriteBuffer(infra::Head(buffer, geometry.RoundToProgramUnit(static_cast<uint32_t>(sealed.size()))), geometry.AddressOfBlock(state.journal.block) + state.journal.end, [this]
            {
                Written();
            });
    }

    void NorJournalWriter::Written()
    {
        volume.State().journal.end += volume.Geometry().RoundToProgramUnit(static_cast<uint32_t>(sealed.size()));

        auto opened = OpenRecord(sealed);
        really_assert(opened.has_value());
        volume.RecordApply().Apply(opened.value_or(OpenedRecord{}), link.has_value(), [this](Result result)
            {
                onDone(result);
            });
    }
}
