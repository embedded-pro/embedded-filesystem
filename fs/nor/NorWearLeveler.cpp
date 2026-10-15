#include "fs/nor/NorWearLeveler.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t blockNumberBytes = 4;
        constexpr std::size_t nodesOfARecord = 2 * norMaxLevels;
    }

    NorWearLeveler::NorWearLeveler(NorVolume& volume)
        : volume(volume)
    {}

    NorWearLeveler::~NorWearLeveler() = default;

    bool NorWearLeveler::Due() const
    {
        return (!erasesAtMove.has_value() || ErasesSinceMove() >= volume.Config().staticWearLevelingInterval) && IsWorthAMove();
    }

    bool NorWearLeveler::FallbackDue() const
    {
        return ErasesSinceMove() >= 2 * volume.Config().staticWearLevelingInterval && IsWorthAMove();
    }

    void NorWearLeveler::Reset()
    {
        erasesAtMove = std::nullopt;
        givenUp = std::nullopt;
    }

    void NorWearLeveler::Move(const infra::Function<void()>& onMoved)
    {
        onDone = onMoved;
        erasesAtMove = BlockErases();

        auto from = Scan().source;
        really_assert(from.has_value());
        source = from.value_or(0);

        if (volume.State().nodeBlocks.Contains(source))
            MoveNodes();
        else
            MoveData();
    }

    uint32_t NorWearLeveler::ErasesSinceMove() const
    {
        return BlockErases() - erasesAtMove.value_or(0);
    }

    uint32_t NorWearLeveler::BlockErases() const
    {
        return volume.Counters().erasedSectors / volume.Config().sectorsPerBlock;
    }

    bool NorWearLeveler::IsWorthAMove() const
    {
        auto candidates = Scan();
        if (!candidates.source.has_value() || !candidates.destination.has_value())
            return false;

        const auto& counts = volume.State().eraseCounts;
        auto lowest = counts.Count(candidates.source.value_or(0));
        return candidates.highest - lowest > volume.Config().staticWearLevelingThreshold && counts.Count(candidates.destination.value_or(0)) > lowest;
    }

    NorWearLeveler::Candidates NorWearLeveler::Scan() const
    {
        const auto& state = volume.State();
        const auto& counts = state.eraseCounts;
        Candidates candidates;

        for (uint32_t candidate = superblocks; candidate != volume.Geometry().blocks; ++candidate)
        {
            auto count = counts.Count(candidate);
            candidates.highest = std::max(candidates.highest, count);

            if (state.blocks.State(candidate) == NorBlockState::freeErased && (!candidates.destination.has_value() || count > counts.Count(candidates.destination.value_or(0))))
                candidates.destination = candidate;
            else if (IsMovable(candidate) && (!candidates.source.has_value() || count < counts.Count(candidates.source.value_or(0))))
                candidates.source = candidate;
        }

        return candidates;
    }

    bool NorWearLeveler::IsMovable(uint32_t block) const
    {
        const auto& state = volume.State();
        if (state.blocks.State(block) != NorBlockState::inUse || block == state.checkpointBlock || block == state.nodeBlock || block == givenUp)
            return false;

        return std::ranges::find(state.epoch, block) == state.epoch.end();
    }

    void NorWearLeveler::MoveNodes()
    {
        givenUp = std::nullopt;
        volume.Collector().Move(source);
        volume.State().nodeBlock = std::nullopt;
        volume.Checkpoints().Write([this](Result)
            {
                Done();
            });
    }

    void NorWearLeveler::MoveData()
    {
        const auto& geometry = volume.Geometry();
        volume.Flash().ReadBuffer(infra::Head(volume.JournalBuffer(), geometry.HeaderSize()), geometry.AddressOfBlock(source), [this]
            {
                HeaderRead();
            });
    }

    void NorWearLeveler::HeaderRead()
    {
        const auto& geometry = volume.Geometry();
        auto allocation = ParseNorBlockHeader(infra::Head(volume.JournalBuffer(), geometry.HeaderSize()), geometry.programUnit).allocation;
        if (!allocation.has_value() || allocation->type != NorBlockType::data || HeldByAHandle())
        {
            GiveUp();
            return;
        }

        owner = allocation->owner;

        volume.Fetch().Fetch(InodeTable(), volume.State().inodeTable, owner.inode, [this](NorFetchResult result)
            {
                RecordFetched(result);
            });
    }

    void NorWearLeveler::RecordFetched(NorFetchResult result)
    {
        auto bytes = result == NorFetchResult::fetched ? NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, owner.inode) : std::nullopt;
        auto record = bytes.has_value() ? ParseNorInodeRecord(*bytes, volume.Config()) : std::nullopt;
        const auto& geometry = volume.Geometry();
        auto consistent = record.has_value() && record->type == NorInodeType::file && record->inlineBytes.size() <= record->size;
        auto blockBytes = consistent ? uint64_t{ record->size } - record->inlineBytes.size() : 0;
        auto start = uint64_t{ owner.index } * geometry.PayloadSize();

        if (!consistent || start >= blockBytes)
        {
            GiveUp();
            return;
        }

        dataMap = NorMapRoot{ record->mapRoot, record->mapHeight };
        length = geometry.RoundToProgramUnit(static_cast<uint32_t>(std::min<uint64_t>(geometry.PayloadSize(), blockBytes - start)));
        volume.Fetch().Fetch(DataMap(), dataMap, owner.index, [this](NorFetchResult fetched)
            {
                MapFetched(fetched);
            });
    }

    void NorWearLeveler::MapFetched(NorFetchResult result)
    {
        const auto& cache = volume.Cache();
        if (result != NorFetchResult::fetched || NorRadixTree(volume.Cache(), owner.inode).Find(dataMap, owner.index) != source || cache.Capacity() - cache.ModifiedNodes() < nodesOfARecord)
        {
            GiveUp();
            return;
        }

        volume.Allocator().Allocate(NorBlockType::data, owner, NorReserve::kept, NorWear::most, [this](std::optional<uint32_t> allocated)
            {
                Allocated(allocated);
            });
    }

    bool NorWearLeveler::HeldByAHandle() const
    {
        return std::ranges::any_of(volume.OpenFiles(), [this](const NorFile& file)
            {
                const auto& state = file.State();
                return state.tailBlock == source || std::ranges::any_of(state.entries, [this](const NorBlockEntry& entry)
                                                        {
                                                            return entry.previous == source;
                                                        });
            });
    }

    void NorWearLeveler::Allocated(std::optional<uint32_t> block)
    {
        if (!block.has_value())
        {
            GiveUp();
            return;
        }

        target = block.value_or(0);
        copied = 0;
        CopyNextChunk();
    }

    void NorWearLeveler::CopyNextChunk()
    {
        if (copied == length)
        {
            Commit();
            return;
        }

        const auto& geometry = volume.Geometry();
        auto buffer = volume.JournalBuffer();
        chunk = std::min(static_cast<uint32_t>(buffer.size()), length - copied);
        volume.Flash().ReadBuffer(infra::Head(buffer, chunk), geometry.AddressOfBlock(source) + geometry.HeaderSize() + copied, [this]
            {
                const auto& programGeometry = volume.Geometry();
                volume.Flash().WriteBuffer(infra::Head(volume.JournalBuffer(), chunk), programGeometry.AddressOfBlock(target) + programGeometry.HeaderSize() + copied, [this]
                    {
                        copied += chunk;
                        CopyNextChunk();
                    });
            });
    }

    void NorWearLeveler::Commit()
    {
        volume.Journal().Commit(norBlockSetEntryBytes, [this](CrcRecord& record)
            {
                AddNorEntry(record, NorBlockSet{ owner.inode, owner.index, source, target });
            },
            [this](Result result)
            {
                Committed(result);
            });
    }

    void NorWearLeveler::Committed(Result result)
    {
        if (result != Result::success)
        {
            // Only a record that was never programmed leaves the copy unreferenced on flash
            if (result == Result::noSpace)
                volume.State().blocks.Set(target, NorBlockState::freeNotErased);

            GiveUp();
            return;
        }

        ++volume.Counters().movedBlocks;
        givenUp = std::nullopt;
        Done();
    }

    void NorWearLeveler::GiveUp()
    {
        givenUp = source;
        Done();
    }

    void NorWearLeveler::Done()
    {
        volume.Cache().UnpinAll();
        onDone();
    }

    NorMapId NorWearLeveler::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorWearLeveler::DataMap() const
    {
        return NorMapId{ NorMap::fileData, owner.inode, blockNumberBytes };
    }
}
