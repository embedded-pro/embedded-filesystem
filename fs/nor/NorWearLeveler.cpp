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
        return DueAfter(volume.Config().staticWearLevelingInterval);
    }

    bool NorWearLeveler::FallbackDue() const
    {
        return DueAfter(2 * volume.Config().staticWearLevelingInterval);
    }

    void NorWearLeveler::Reset()
    {
        erasesAtMove = 0;
    }

    void NorWearLeveler::Move(const infra::Function<void()>& onMoved)
    {
        onDone = onMoved;
        erasesAtMove = BlockErases();

        auto from = Source();
        really_assert(from.has_value());
        source = from.value_or(0);

        if (volume.State().nodeBlocks.Contains(source))
            MoveNodes();
        else
            MoveData();
    }

    bool NorWearLeveler::DueAfter(uint32_t erases) const
    {
        return BlockErases() - erasesAtMove >= erases && IsWorthAMove();
    }

    uint32_t NorWearLeveler::BlockErases() const
    {
        return volume.Counters().erasedSectors / volume.Config().sectorsPerBlock;
    }

    bool NorWearLeveler::IsWorthAMove() const
    {
        auto from = Source();
        auto to = MostWornErased();
        if (!from.has_value() || !to.has_value())
            return false;

        const auto& counts = volume.State().eraseCounts;
        auto lowest = counts.Count(from.value_or(0));
        return HighestCount() - lowest > volume.Config().staticWearLevelingThreshold && counts.Count(to.value_or(0)) > lowest;
    }

    std::optional<uint32_t> NorWearLeveler::Source() const
    {
        const auto& counts = volume.State().eraseCounts;
        std::optional<uint32_t> leastWorn;

        for (uint32_t candidate = superblocks; candidate != volume.Geometry().blocks; ++candidate)
            if (IsMovable(candidate) && (!leastWorn.has_value() || counts.Count(candidate) < counts.Count(leastWorn.value_or(0))))
                leastWorn = candidate;

        return leastWorn;
    }

    std::optional<uint32_t> NorWearLeveler::MostWornErased() const
    {
        const auto& state = volume.State();
        std::optional<uint32_t> mostWorn;

        for (uint32_t candidate = superblocks; candidate != volume.Geometry().blocks; ++candidate)
            if (state.blocks.State(candidate) == NorBlockState::freeErased && (!mostWorn.has_value() || state.eraseCounts.Count(candidate) > state.eraseCounts.Count(mostWorn.value_or(0))))
                mostWorn = candidate;

        return mostWorn;
    }

    uint32_t NorWearLeveler::HighestCount() const
    {
        uint32_t highest = 0;
        for (uint32_t candidate = superblocks; candidate != volume.Geometry().blocks; ++candidate)
            highest = std::max(highest, volume.State().eraseCounts.Count(candidate));

        return highest;
    }

    bool NorWearLeveler::IsMovable(uint32_t block) const
    {
        const auto& state = volume.State();
        auto ofEpoch = std::ranges::find(state.epoch, block) != state.epoch.end();
        return state.blocks.State(block) == NorBlockState::inUse && block != state.checkpointBlock && !ofEpoch && block != state.nodeBlock;
    }

    void NorWearLeveler::MoveNodes()
    {
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
        if (!allocation.has_value() || allocation->type != NorBlockType::data)
        {
            Done();
            return;
        }

        owner = allocation->owner;
        if (OpenForWriting())
        {
            Done();
            return;
        }

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
        auto blockBytes = record.has_value() ? uint64_t{ record->size } - record->inlineBytes.size() : 0;
        auto start = uint64_t{ owner.index } * geometry.PayloadSize();

        if (!record.has_value() || record->type != NorInodeType::file || start >= blockBytes)
        {
            Done();
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
            Done();
            return;
        }

        volume.Allocator().Allocate(NorBlockType::data, owner, NorReserve::kept, NorWear::most, [this](std::optional<uint32_t> allocated)
            {
                Allocated(allocated);
            });
    }

    bool NorWearLeveler::OpenForWriting() const
    {
        return std::ranges::any_of(volume.OpenFiles(), [this](const NorFile& file)
            {
                return file.State().inode == owner.inode && file.State().Writable();
            });
    }

    void NorWearLeveler::Allocated(std::optional<uint32_t> block)
    {
        if (!block.has_value())
        {
            Done();
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
        if (result == Result::success)
            ++volume.Counters().movedBlocks;
        else
            volume.State().blocks.Set(target, NorBlockState::freeNotErased);

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
