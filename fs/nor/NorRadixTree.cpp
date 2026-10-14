#include "fs/nor/NorRadixTree.hpp"
#include "fs/common/LittleEndian.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t slotBytes = 4;

        uint64_t Span(uint32_t fanout, uint8_t level)
        {
            uint64_t span = 1;

            for (uint8_t below = 0; below != level; ++below)
                span *= fanout;

            return span;
        }

        uint32_t FanoutOf(infra::ConstByteRange node)
        {
            return NorRadixFanout(static_cast<uint32_t>(node.size()));
        }

        uint16_t CountOf(infra::ConstByteRange node)
        {
            return ReadNorNodeHeader(node).count;
        }

        uint32_t SlotAt(infra::ConstByteRange node, std::size_t position)
        {
            return LoadLittle32(NorNodeEntries(node), position * slotBytes);
        }

        uint32_t PositionOf(infra::ConstByteRange node, uint32_t index)
        {
            auto header = ReadNorNodeHeader(node);
            return static_cast<uint32_t>((index - header.lowestKey) / Span(FanoutOf(node), header.level));
        }

        void SetSlot(infra::ByteRange node, std::size_t position, uint32_t value)
        {
            auto header = ReadNorNodeHeader(node);
            if ((SlotAt(node, position) == 0) != (value == 0))
                header.count = static_cast<uint16_t>(value != 0 ? header.count + 1 : header.count - 1);

            StoreLittle32(NorNodeEntries(node), position * slotBytes, value);
            WriteNorNodeHeader(header, node);
        }
    }

    NorRadixTree::NorRadixTree(NorNodeCache& cache, uint32_t owner)
        : cache(cache)
        , owner(owner)
    {}

    uint32_t NorRadixTree::Find(const NorMapRoot& root, uint32_t index) const
    {
        auto path = Walk(root, index);
        if (!path.complete)
            return 0;

        return Leaf(path);
    }

    std::optional<uint32_t> NorRadixTree::NodesModifiedBySet(const NorMapRoot& root, uint32_t index) const
    {
        auto height = HeightFor(index);
        if (!height.has_value())
            return std::nullopt;

        auto needed = height.value_or(0);
        if (root.address == 0)
            return needed;

        if (needed > root.height)
            return needed - root.height + needed - 1;

        auto path = Walk(root, index);
        return UnmodifiedNodes(path) + (path.complete ? 0 : root.height - static_cast<uint32_t>(path.nodes));
    }

    std::optional<uint32_t> NorRadixTree::NodesCreatedBySet(const NorMapRoot& root, uint32_t index) const
    {
        auto modified = NodesModifiedBySet(root, index);
        if (!modified.has_value() || root.address == 0 || HeightFor(index).value_or(0) > root.height)
            return modified;

        return modified.value_or(0) - UnmodifiedNodes(Walk(root, index));
    }

    uint32_t NorRadixTree::NodesModifiedByClear(const NorMapRoot& root, uint32_t index) const
    {
        auto path = Walk(root, index);
        if (!Maps(path))
            return 0;

        return UnmodifiedNodes(path);
    }

    void NorRadixTree::Set(NorMapRoot& root, uint32_t index, uint32_t block) const
    {
        auto height = HeightFor(index);
        really_assert(height.has_value() && block != 0);

        auto needed = height.value_or(0);
        while (root.address != 0 && root.height < needed)
        {
            auto top = cache.Create(NorNodeHeader{ NorMap::fileData, root.height, 0, owner, 0 });
            SetSlot(cache.Modify(top), 0, root.address);
            root = NorMapRoot{ cache.AddressOf(top), static_cast<uint8_t>(root.height + 1) };
        }

        if (root.address == 0)
            root.height = needed;

        auto fanout = NorRadixFanout(cache.NodeSize());
        auto address = root.address;
        auto level = static_cast<uint8_t>(root.height - 1);
        uint32_t first = 0;
        std::size_t parent = 0;
        uint32_t parentPosition = 0;

        for (std::size_t depth = 0;; ++depth)
        {
            std::size_t slot = 0;
            if (address == 0)
                slot = cache.Create(NorNodeHeader{ NorMap::fileData, level, 0, owner, first });
            else
            {
                auto found = cache.Find(address);
                really_assert(found.has_value());
                slot = found.value_or(0);
                cache.Modify(slot);
            }

            if (depth == 0)
                root.address = cache.AddressOf(slot);
            else
                SetSlot(cache.Modify(parent), parentPosition, cache.AddressOf(slot));

            auto span = Span(fanout, level);
            auto position = static_cast<uint32_t>((index - first) / span);
            if (level == 0)
            {
                SetSlot(cache.Modify(slot), position, block);
                return;
            }

            parent = slot;
            parentPosition = position;
            address = SlotAt(cache.Node(slot), position);
            first += static_cast<uint32_t>(position * span);
            --level;
        }
    }

    void NorRadixTree::Clear(NorMapRoot& root, uint32_t index) const
    {
        auto path = Walk(root, index);
        if (!Maps(path))
            return;

        for (std::size_t depth = 0; depth != path.nodes; ++depth)
        {
            cache.Modify(path.slots[depth]);
            if (depth != 0)
                SetSlot(cache.Modify(path.slots[depth - 1]), path.positions[depth - 1], cache.AddressOf(path.slots[depth]));
        }

        root.address = cache.AddressOf(path.slots[0]);

        auto depth = path.nodes - 1;
        SetSlot(cache.Modify(path.slots[depth]), path.positions[depth], 0);

        while (depth != 0 && CountOf(cache.Node(path.slots[depth])) == 0)
        {
            cache.Drop(path.slots[depth]);
            --depth;
            SetSlot(cache.Modify(path.slots[depth]), path.positions[depth], 0);
        }

        if (CountOf(cache.Node(path.slots[0])) == 0)
        {
            cache.Drop(path.slots[0]);
            root = NorMapRoot{};
            return;
        }

        for (std::size_t top = 0; root.height > 1 && CountOf(cache.Node(path.slots[top])) == 1 && SlotAt(cache.Node(path.slots[top]), 0) != 0; ++top)
        {
            auto child = SlotAt(cache.Node(path.slots[top]), 0);
            auto onPath = child == cache.AddressOf(path.slots[top + 1]);
            cache.Drop(path.slots[top]);
            root = NorMapRoot{ child, static_cast<uint8_t>(root.height - 1) };

            if (!onPath)
                return;
        }
    }

    bool NorRadixTree::Covers(const NorMapRoot& root, uint32_t index, uint32_t nodeSize)
    {
        return root.address != 0 && index < Span(NorRadixFanout(nodeSize), root.height);
    }

    bool NorRadixTree::CanMap(uint32_t index, uint32_t nodeSize)
    {
        return index < Span(NorRadixFanout(nodeSize), norMaxLevels);
    }

    uint32_t NorRadixTree::ChildFor(infra::ConstByteRange node, uint32_t index)
    {
        return SlotAt(node, PositionOf(node, index));
    }

    uint32_t NorRadixTree::FirstIndexOfChild(infra::ConstByteRange node, uint32_t index)
    {
        auto header = ReadNorNodeHeader(node);
        return header.lowestKey + static_cast<uint32_t>(PositionOf(node, index) * Span(FanoutOf(node), header.level));
    }

    bool NorRadixTree::HasValidCount(infra::ConstByteRange node)
    {
        auto fanout = FanoutOf(node);
        uint32_t set = 0;

        for (std::size_t position = 0; position != fanout; ++position)
            if (SlotAt(node, position) != 0)
                ++set;

        return set != 0 && set == CountOf(node);
    }

    bool NorRadixTree::ReplaceChild(infra::ByteRange node, uint32_t from, uint32_t to)
    {
        auto fanout = FanoutOf(node);

        for (std::size_t position = 0; position != fanout; ++position)
            if (SlotAt(node, position) == from)
            {
                SetSlot(node, position, to);
                return true;
            }

        return false;
    }

    NorRadixTree::Path NorRadixTree::Walk(const NorMapRoot& root, uint32_t index) const
    {
        Path path;
        if (!Covers(root, index, cache.NodeSize()))
            return path;

        auto address = root.address;
        while (address != 0)
        {
            auto found = cache.Find(address);
            really_assert(found.has_value());

            auto slot = found.value_or(0);
            auto node = cache.Node(slot);
            auto position = PositionOf(node, index);
            path.slots[path.nodes] = slot;
            path.positions[path.nodes] = position;
            ++path.nodes;

            if (ReadNorNodeHeader(node).level == 0)
            {
                path.complete = true;
                return path;
            }

            address = SlotAt(node, position);
        }

        return path;
    }

    uint32_t NorRadixTree::Leaf(const Path& path) const
    {
        return SlotAt(cache.Node(path.slots[path.nodes - 1]), path.positions[path.nodes - 1]);
    }

    bool NorRadixTree::Maps(const Path& path) const
    {
        return path.complete && Leaf(path) != 0;
    }

    uint32_t NorRadixTree::UnmodifiedNodes(const Path& path) const
    {
        return static_cast<uint32_t>(std::ranges::count_if(path.slots.begin(), path.slots.begin() + path.nodes, [this](std::size_t slot)
            {
                return !cache.IsModified(slot);
            }));
    }

    std::optional<uint8_t> NorRadixTree::HeightFor(uint32_t index) const
    {
        auto fanout = NorRadixFanout(cache.NodeSize());

        for (uint8_t height = 1; height <= norMaxLevels; ++height)
            if (index < Span(fanout, height))
                return height;

        return std::nullopt;
    }
}
