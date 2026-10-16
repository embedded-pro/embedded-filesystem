#include "fs/nor/NorNodeCollector.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t mapValueBytes = 4;
        constexpr uint32_t checkpointBlocks = 1;
        constexpr std::size_t nodesOfARelocation = 2 * norMaxLevels;
    }

    NorNodeCollector::NorNodeCollector(NorVolume& volume)
        : volume(volume)
    {}

    NorNodeCollector::~NorNodeCollector() = default;

    bool NorNodeCollector::Due() const
    {
        return DueByRatio() || DueForSpace();
    }

    bool NorNodeCollector::DueForSpace() const
    {
        const auto& geometry = volume.Geometry();
        const auto& blocks = volume.State().blocks;
        auto free = blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased);
        return free <= geometry.ReserveBlocks() + geometry.WatermarkBlocks() && UnusedSlots() >= unusedAtFruitlessLap.value_or(0) + geometry.NodesPerBlock();
    }

    bool NorNodeCollector::DueByRatio() const
    {
        auto collectable = CollectableBlocks();
        return collectable >= 2 && uint64_t{ collectable } * volume.Geometry().NodesPerBlock() > 2 * LiveNodes();
    }

    uint64_t NorNodeCollector::UnusedSlots() const
    {
        auto slots = uint64_t{ CollectableBlocks() } * volume.Geometry().NodesPerBlock();
        auto live = LiveNodes();
        return slots > live ? slots - live : 0;
    }

    uint64_t NorNodeCollector::LiveNodes() const
    {
        const auto& cache = volume.Cache();
        return uint64_t{ volume.State().liveNodes } + cache.ModifiedNodes() - cache.ReleasedNodes();
    }

    uint32_t NorNodeCollector::CollectableBlocks() const
    {
        const auto& state = volume.State();
        auto filling = state.nodeBlock.has_value() && state.nodeBlocks.Contains(state.nodeBlock.value_or(0)) ? 1 : 0;
        return state.nodeBlocks.Count() - awaitingCommit - filling;
    }

    uint32_t NorNodeCollector::LeftBlocks() const
    {
        return left;
    }

    bool NorNodeCollector::Moving() const
    {
        return moving.has_value();
    }

    void NorNodeCollector::Reset()
    {
        block = std::nullopt;
        slot = 0;
        moving = std::nullopt;
        movePassed = false;
        awaitingCommit = 0;
        surveying = false;
        skippedInARow = 0;
        unusedAtFruitlessLap = std::nullopt;
    }

    void NorNodeCollector::Move(uint32_t moved)
    {
        moving = moved;
        block = moved;
        slot = 0;
        surveying = false;
    }

    void NorNodeCollector::Collect(bool collectionDue, const infra::Function<void()>& onCollected)
    {
        collecting = collectionDue;
        sparseOnly = !DueByRatio();
        onDone = onCollected;
        leftByStep = 0;
        freeSlots = FreeSlots();
        volume.Cache().UnpinAll();
        Examine();
    }

    void NorNodeCollector::Committed()
    {
        if (movePassed)
            ++volume.Counters().movedBlocks;

        movePassed = false;
        auto& state = volume.State();
        for (uint32_t candidate = superblocks; awaitingCommit != 0 && candidate != state.blocks.Blocks(); ++candidate)
            if (state.nodeBlocks.Contains(candidate) && state.blocks.State(candidate) == NorBlockState::pending)
            {
                state.blocks.Set(candidate, NorBlockState::freeNotErased);
                state.nodeBlocks.Remove(candidate);
                --awaitingCommit;
            }
    }

    void NorNodeCollector::Examine()
    {
        const auto& geometry = volume.Geometry();
        if (leftByStep == 2 * geometry.NodeBlocksPerCheckpoint() || !HasRoom() || (!collecting && !moving.has_value()))
        {
            onDone();
            return;
        }

        if (!block.has_value() || slot == geometry.NodesPerBlock() || !IsCollectable(block.value_or(0)))
        {
            block = NextBlock();
            slot = 0;
            surveying = sparseOnly;
            liveInBlock = 0;
        }

        if (!block.has_value())
        {
            onDone();
            return;
        }

        volume.Flash().ReadBuffer(infra::MakeRange(headerBytes), SlotAddress(), [this]
            {
                HeaderRead();
            });
    }

    void NorNodeCollector::HeaderRead()
    {
        header = ReadNorNodeHeader(infra::MakeRange(headerBytes));
        if (!IsNorNodeHeader(header))
            Next();
        else if (header.map == NorMap::inodeTable)
            Check(volume.State().inodeTable);
        else
            volume.Fetch().Fetch(InodeTable(), volume.State().inodeTable, header.owner, [this](NorFetchResult result)
                {
                    OwnerFetched(result);
                });
    }

    void NorNodeCollector::OwnerFetched(NorFetchResult result)
    {
        really_assert(result != NorFetchResult::cacheFull);

        if (result != NorFetchResult::fetched)
        {
            Skip();
            return;
        }

        auto record = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, header.owner);
        auto type = header.map == NorMap::directory ? NorInodeType::directory : NorInodeType::file;
        if (!record.has_value() || (*record)[norInodeTypeOffset] != static_cast<uint8_t>(type))
            Next();
        else
            Check(NorMapRoot{ LoadLittle32(*record, norInodeMapRootOffset), (*record)[norInodeMapHeightOffset] });
    }

    void NorNodeCollector::Check(const NorMapRoot& map)
    {
        root = map;
        if (header.level >= root.height || (header.map == NorMap::fileData && !NorRadixTree::Covers(root, header.lowestKey, volume.Cache().NodeSize())))
            Next();
        else if (header.level + 1 == root.height)
            Compare(root.address);
        else
            volume.Fetch().Fetch(Map(), root, header.lowestKey, static_cast<uint8_t>(header.level + 1), [this](NorFetchResult result)
                {
                    ParentFetched(result);
                });
    }

    void NorNodeCollector::ParentFetched(NorFetchResult result)
    {
        really_assert(result != NorFetchResult::cacheFull);

        if (result != NorFetchResult::fetched)
            Skip();
        else
            Compare(AddressAt(header.level));
    }

    void NorNodeCollector::Compare(uint32_t address)
    {
        if (address != SlotAddress())
            Next();
        else if (surveying)
        {
            ++liveInBlock;
            Next();
        }
        else
            volume.Fetch().Fetch(Map(), root, header.lowestKey, header.level, [this](NorFetchResult result)
                {
                    NodeFetched(result);
                });
    }

    void NorNodeCollector::NodeFetched(NorFetchResult result)
    {
        really_assert(result != NorFetchResult::cacheFull);

        if (result != NorFetchResult::fetched)
            Skip();
        else
        {
            Relocate();
            Next();
        }
    }

    void NorNodeCollector::Relocate() const
    {
        auto& cache = volume.Cache();
        std::array<std::size_t, norMaxLevels> path{};
        auto nodes = static_cast<std::size_t>(root.height - header.level);
        auto address = root.address;

        for (std::size_t depth = 0; depth != nodes; ++depth)
        {
            auto found = cache.Find(address);
            really_assert(found.has_value());
            path[depth] = found.value_or(0);
            if (depth + 1 != nodes)
                address = ChildOf(path[depth]);
        }

        auto relocated = root;
        for (std::size_t depth = 0; depth != nodes; ++depth)
        {
            auto from = cache.AddressOf(path[depth]);
            cache.Modify(path[depth]);

            if (depth == 0)
                relocated.address = cache.AddressOf(path[depth]);
            else
            {
                auto parent = cache.Modify(path[depth - 1]);
                auto replaced = header.map == NorMap::fileData ? NorRadixTree::ReplaceChild(parent, from, cache.AddressOf(path[depth])) : NorBTree::ReplaceChild(parent, from, cache.AddressOf(path[depth]));
                really_assert(replaced);
            }
        }

        if (header.map == NorMap::inodeTable)
            volume.State().inodeTable = relocated;
        else
            StoreLittle32(NorBTree(cache, InodeTable()).Modify(volume.State().inodeTable, header.owner), norInodeMapRootOffset, relocated.address);
    }

    void NorNodeCollector::Next()
    {
        volume.Cache().UnpinAll();
        if (++slot != volume.Geometry().NodesPerBlock())
            Examine();
        else if (surveying)
            EndSurvey();
        else
        {
            Pass();
            Examine();
        }
    }

    void NorNodeCollector::EndSurvey()
    {
        surveying = false;
        if (2 * liveInBlock < volume.Geometry().NodesPerBlock())
            slot = 0;
        else
        {
            Leave();
            if (++skippedInARow >= CollectableBlocks())
                unusedAtFruitlessLap = UnusedSlots();
        }

        Examine();
    }

    void NorNodeCollector::Skip()
    {
        volume.Cache().UnpinAll();
        surveying = false;
        slot = volume.Geometry().NodesPerBlock();
        Leave();
        Examine();
    }

    void NorNodeCollector::Pass()
    {
        skippedInARow = 0;
        unusedAtFruitlessLap = std::nullopt;
        volume.State().blocks.Set(block.value_or(0), NorBlockState::pending);
        ++awaitingCommit;
        movePassed = movePassed || block == moving;
        Leave();
    }

    void NorNodeCollector::Leave()
    {
        if (block == moving)
            moving = std::nullopt;

        ++left;
        ++leftByStep;
    }

    bool NorNodeCollector::HasRoom() const
    {
        const auto& cache = volume.Cache();
        auto modified = cache.ModifiedNodes();
        return modified + nodesOfARelocation <= cache.Capacity() && modified + nodesOfARelocation <= freeSlots;
    }

    uint32_t NorNodeCollector::FreeSlots() const
    {
        const auto& state = volume.State();
        const auto& geometry = volume.Geometry();
        auto free = state.blocks.Count(NorBlockState::freeErased) + state.blocks.Count(NorBlockState::freeNotErased);
        auto filling = state.nodeBlock.has_value() ? (geometry.BlockSize() - state.nodeOffset) / geometry.nodeSize : 0;
        return (free > checkpointBlocks ? free - checkpointBlocks : 0) * geometry.NodesPerBlock() + filling;
    }

    uint32_t NorNodeCollector::AddressAt(uint8_t level) const
    {
        auto address = root.address;
        for (uint32_t at = root.height - 1u; at != level && address != 0; --at)
        {
            auto found = volume.Cache().Find(address);
            really_assert(found.has_value());
            address = ChildOf(found.value_or(0));
        }

        return address;
    }

    uint32_t NorNodeCollector::ChildOf(std::size_t node) const
    {
        auto bytes = volume.Cache().Node(node);
        return header.map == NorMap::fileData ? NorRadixTree::ChildFor(bytes, header.lowestKey) : NorBTree::ChildFor(bytes, header.lowestKey);
    }

    bool NorNodeCollector::IsCollectable(uint32_t candidate) const
    {
        const auto& state = volume.State();
        return state.nodeBlocks.Contains(candidate) && state.blocks.State(candidate) == NorBlockState::inUse && candidate != state.nodeBlock;
    }

    std::optional<uint32_t> NorNodeCollector::NextBlock() const
    {
        auto blocks = volume.Geometry().blocks - superblocks;
        auto start = block.has_value() ? block.value_or(0) + 1 - superblocks : 0;

        for (uint32_t step = 0; step != blocks; ++step)
        {
            auto candidate = superblocks + (start + step) % blocks;
            if (IsCollectable(candidate))
                return candidate;
        }

        return std::nullopt;
    }

    uint32_t NorNodeCollector::SlotAddress() const
    {
        const auto& geometry = volume.Geometry();
        return geometry.AddressOfBlock(block.value_or(0)) + geometry.NodeSlotsOffset() + slot * geometry.nodeSize;
    }

    NorMapId NorNodeCollector::Map() const
    {
        if (header.map == NorMap::inodeTable)
            return InodeTable();

        return NorMapId{ header.map, header.owner, mapValueBytes };
    }

    NorMapId NorNodeCollector::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }
}
