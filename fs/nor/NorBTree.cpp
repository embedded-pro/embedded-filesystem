#include "fs/nor/NorBTree.hpp"
#include "fs/common/LittleEndian.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t keyBytes = 4;
        constexpr uint32_t innerEntryBytes = 8;

        uint16_t CountOf(infra::ConstByteRange node)
        {
            return ReadNorNodeHeader(node).count;
        }

        uint32_t KeyAt(infra::ConstByteRange node, uint32_t entryBytes, std::size_t index)
        {
            return LoadLittle32(NorNodeEntries(node), index * entryBytes);
        }

        uint32_t ChildAt(infra::ConstByteRange node, std::size_t index)
        {
            return LoadLittle32(NorNodeEntries(node), index * innerEntryBytes + keyBytes);
        }

        uint16_t ChildIndex(infra::ConstByteRange node, uint32_t key)
        {
            uint16_t index = 0;
            auto count = CountOf(node);

            while (index + 1 < count && KeyAt(node, innerEntryBytes, index + 1u) <= key)
                ++index;

            return index;
        }

        uint16_t LowerBound(infra::ConstByteRange node, uint32_t entryBytes, uint32_t key)
        {
            uint16_t index = 0;
            auto count = CountOf(node);

            while (index < count && KeyAt(node, entryBytes, index) < key)
                ++index;

            return index;
        }

        infra::ByteRange EntryAt(infra::ByteRange node, uint32_t entryBytes, std::size_t index)
        {
            return infra::Head(infra::DiscardHead(NorNodeEntries(node), index * entryBytes), entryBytes);
        }

        void SetCount(infra::ByteRange node, uint32_t entryBytes, uint16_t count)
        {
            auto header = ReadNorNodeHeader(node);
            header.count = count;
            if (count != 0)
                header.lowestKey = KeyAt(node, entryBytes, 0);

            WriteNorNodeHeader(header, node);
        }
    }

    NorBTree::NorBTree(NorNodeCache& cache, const NorMapId& map)
        : cache(cache)
        , map(map)
    {}

    std::optional<infra::ConstByteRange> NorBTree::Find(const NorMapRoot& root, uint32_t key) const
    {
        auto path = Walk(root, key);
        if (!Holds(path, key))
            return std::nullopt;

        auto leaf = cache.Node(path.slots[path.nodes - 1]);
        return infra::Head(infra::DiscardHead(NorNodeEntries(leaf), path.positions[path.nodes - 1] * EntryBytes(0) + keyBytes), map.valueBytes);
    }

    std::optional<uint32_t> NorBTree::NodesModifiedByInsert(const NorMapRoot& root, uint32_t key) const
    {
        if (root.address == 0)
            return 1;

        auto path = Walk(root, key);
        auto modified = UnmodifiedNodes(path);

        for (std::size_t depth = path.nodes; depth-- != 0;)
        {
            auto level = static_cast<uint8_t>(path.nodes - 1 - depth);
            if (CountOf(cache.Node(path.slots[depth])) < Capacity(level))
                return modified;

            ++modified;
        }

        if (root.height == norMaxLevels)
            return std::nullopt;

        return modified + 1;
    }

    uint32_t NorBTree::NodesModifiedByChange(const NorMapRoot& root, uint32_t key) const
    {
        return UnmodifiedNodes(Walk(root, key));
    }

    infra::ByteRange NorBTree::Insert(NorMapRoot& root, uint32_t key) const
    {
        if (root.address == 0)
        {
            auto slot = cache.Create(NorNodeHeader{ map.map, 0, 0, map.owner, key });
            root = NorMapRoot{ cache.AddressOf(slot), 1 };
            return infra::DiscardHead(InsertEntry(slot, 0, key), keyBytes);
        }

        auto path = Walk(root, key);
        really_assert(!Holds(path, key));

        ModifyPath(root, path);
        LowerFirstKeys(path, key);

        infra::ByteRange inserted;
        auto depth = path.nodes - 1;
        auto position = path.positions[depth];
        uint32_t child = 0;

        for (uint8_t level = 0;; ++level)
        {
            auto slot = path.slots[depth];
            auto count = CountOf(cache.Node(slot));
            auto split = count == Capacity(level);
            auto target = slot;
            auto targetPosition = position;
            std::size_t right = 0;

            if (split)
            {
                right = cache.Create(NorNodeHeader{ map.map, level, 0, map.owner, 0 });
                auto kept = static_cast<uint16_t>(position == count ? count : count - count / 2);
                MoveTail(slot, right, static_cast<uint16_t>(count - kept));

                if (position >= kept)
                {
                    target = right;
                    targetPosition = static_cast<uint16_t>(position - kept);
                }
            }

            auto entry = InsertEntry(target, targetPosition, key);
            if (level == 0)
                inserted = infra::DiscardHead(entry, keyBytes);
            else
                StoreLittle32(entry, keyBytes, child);

            if (!split)
                return inserted;

            key = KeyAt(cache.Node(right), EntryBytes(level), 0);
            child = cache.AddressOf(right);

            if (depth == 0)
            {
                really_assert(root.height < norMaxLevels);

                auto top = cache.Create(NorNodeHeader{ map.map, static_cast<uint8_t>(level + 1), 0, map.owner, 0 });
                StoreLittle32(InsertEntry(top, 0, KeyAt(cache.Node(slot), EntryBytes(level), 0)), keyBytes, cache.AddressOf(slot));
                StoreLittle32(InsertEntry(top, 1, key), keyBytes, child);
                root = NorMapRoot{ cache.AddressOf(top), static_cast<uint8_t>(root.height + 1) };
                return inserted;
            }

            --depth;
            position = static_cast<uint16_t>(path.positions[depth] + 1);
        }
    }

    infra::ByteRange NorBTree::Modify(NorMapRoot& root, uint32_t key) const
    {
        auto path = Walk(root, key);
        really_assert(Holds(path, key));

        ModifyPath(root, path);
        auto leaf = cache.Modify(path.slots[path.nodes - 1]);
        return infra::DiscardHead(EntryAt(leaf, EntryBytes(0), path.positions[path.nodes - 1]), keyBytes);
    }

    void NorBTree::Remove(NorMapRoot& root, uint32_t key) const
    {
        auto path = Walk(root, key);
        really_assert(Holds(path, key));

        ModifyPath(root, path);

        auto depth = path.nodes - 1;
        RemoveEntry(path.slots[depth], path.positions[depth]);

        while (depth != 0 && CountOf(cache.Node(path.slots[depth])) == 0)
        {
            cache.Drop(path.slots[depth]);
            --depth;
            RemoveEntry(path.slots[depth], path.positions[depth]);
        }

        if (CountOf(cache.Node(path.slots[0])) == 0)
        {
            cache.Drop(path.slots[0]);
            root = NorMapRoot{};
            return;
        }

        for (std::size_t top = 0; root.height > 1 && CountOf(cache.Node(path.slots[top])) == 1; ++top)
        {
            auto child = ChildAt(cache.Node(path.slots[top]), 0);
            auto onPath = child == cache.AddressOf(path.slots[top + 1]);
            cache.Drop(path.slots[top]);
            root = NorMapRoot{ child, static_cast<uint8_t>(root.height - 1) };

            if (!onPath)
                return;
        }
    }

    uint32_t NorBTree::ChildFor(infra::ConstByteRange node, uint32_t key)
    {
        return ChildAt(node, ChildIndex(node, key));
    }

    bool NorBTree::HasValidCount(infra::ConstByteRange node, uint32_t valueBytes)
    {
        auto header = ReadNorNodeHeader(node);
        auto entryBytes = header.level == 0 ? keyBytes + valueBytes : innerEntryBytes;
        return header.count != 0 && header.count <= NorNodeEntryBytes(static_cast<uint32_t>(node.size())) / entryBytes;
    }

    bool NorBTree::ReplaceChild(infra::ByteRange node, uint32_t from, uint32_t to)
    {
        auto count = CountOf(node);

        for (std::size_t index = 0; index != count; ++index)
            if (ChildAt(node, index) == from)
            {
                StoreLittle32(NorNodeEntries(node), index * innerEntryBytes + keyBytes, to);
                return true;
            }

        return false;
    }

    uint32_t NorBTree::EntryBytes(uint8_t level) const
    {
        return level == 0 ? keyBytes + map.valueBytes : innerEntryBytes;
    }

    uint32_t NorBTree::Capacity(uint8_t level) const
    {
        return NorNodeEntryBytes(cache.NodeSize()) / EntryBytes(level);
    }

    NorBTree::Path NorBTree::Walk(const NorMapRoot& root, uint32_t key) const
    {
        Path path;
        auto address = root.address;

        for (auto level = root.height; level-- != 0;)
        {
            auto found = cache.Find(address);
            really_assert(found.has_value());

            auto slot = found.value_or(0);
            auto node = cache.Node(slot);
            path.slots[path.nodes] = slot;

            if (level == 0)
                path.positions[path.nodes] = LowerBound(node, EntryBytes(0), key);
            else
            {
                path.positions[path.nodes] = ChildIndex(node, key);
                address = ChildAt(node, path.positions[path.nodes]);
            }

            ++path.nodes;
        }

        return path;
    }

    bool NorBTree::Holds(const Path& path, uint32_t key) const
    {
        if (path.nodes == 0)
            return false;

        auto leaf = cache.Node(path.slots[path.nodes - 1]);
        auto position = path.positions[path.nodes - 1];
        return position < CountOf(leaf) && KeyAt(leaf, EntryBytes(0), position) == key;
    }

    uint32_t NorBTree::UnmodifiedNodes(const Path& path) const
    {
        return static_cast<uint32_t>(std::ranges::count_if(path.slots.begin(), path.slots.begin() + path.nodes, [this](std::size_t slot)
            {
                return !cache.IsModified(slot);
            }));
    }

    void NorBTree::ModifyPath(NorMapRoot& root, const Path& path) const
    {
        for (std::size_t depth = 0; depth != path.nodes; ++depth)
            cache.Modify(path.slots[depth]);

        for (std::size_t depth = 1; depth < path.nodes; ++depth)
            StoreLittle32(EntryAt(cache.Modify(path.slots[depth - 1]), innerEntryBytes, path.positions[depth - 1]), keyBytes, cache.AddressOf(path.slots[depth]));

        root.address = cache.AddressOf(path.slots[0]);
    }

    void NorBTree::LowerFirstKeys(const Path& path, uint32_t key) const
    {
        for (std::size_t depth = 0; depth + 1 < path.nodes; ++depth)
        {
            auto node = cache.Modify(path.slots[depth]);
            if (path.positions[depth] == 0 && key < KeyAt(node, innerEntryBytes, 0))
            {
                StoreLittle32(NorNodeEntries(node), 0, key);
                SetCount(node, innerEntryBytes, CountOf(node));
            }
        }
    }

    infra::ByteRange NorBTree::InsertEntry(std::size_t slot, uint16_t position, uint32_t key) const
    {
        auto node = cache.Modify(slot);
        auto entryBytes = EntryBytes(ReadNorNodeHeader(node).level);
        auto entries = NorNodeEntries(node);
        auto count = CountOf(node);
        auto at = entries.begin() + position * entryBytes;

        std::ranges::copy_backward(at, entries.begin() + count * entryBytes, entries.begin() + (count + 1) * entryBytes);
        std::ranges::fill(at, at + entryBytes, uint8_t{ 0 });
        StoreLittle32(entries, position * entryBytes, key);
        SetCount(node, entryBytes, static_cast<uint16_t>(count + 1));
        return EntryAt(node, entryBytes, position);
    }

    void NorBTree::RemoveEntry(std::size_t slot, uint16_t position) const
    {
        auto node = cache.Modify(slot);
        auto entryBytes = EntryBytes(ReadNorNodeHeader(node).level);
        auto entries = NorNodeEntries(node);
        auto count = CountOf(node);
        auto end = entries.begin() + count * entryBytes;

        std::ranges::copy(entries.begin() + (position + 1) * entryBytes, end, entries.begin() + position * entryBytes);
        std::ranges::fill(end - entryBytes, end, uint8_t{ 0 });
        SetCount(node, entryBytes, static_cast<uint16_t>(count - 1));
    }

    void NorBTree::MoveTail(std::size_t from, std::size_t to, uint16_t entries) const
    {
        auto source = cache.Modify(from);
        auto target = cache.Modify(to);
        auto entryBytes = EntryBytes(ReadNorNodeHeader(source).level);
        auto count = CountOf(source);
        auto kept = static_cast<uint16_t>(count - entries);
        auto sourceEntries = NorNodeEntries(source);
        auto begin = sourceEntries.begin() + kept * entryBytes;
        auto end = sourceEntries.begin() + count * entryBytes;

        std::ranges::copy(begin, end, NorNodeEntries(target).begin());
        std::ranges::fill(begin, end, uint8_t{ 0 });
        SetCount(source, entryBytes, kept);
        SetCount(target, entryBytes, entries);
    }
}
