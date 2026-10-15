#include "fs/nor/NorOpenFile.hpp"
#include <algorithm>

namespace fs
{
    bool NorOpenFile::Writable() const
    {
        return mode != OpenMode::read;
    }

    bool NorOpenFile::Uncommitted() const
    {
        return size != committedSize || !entries.empty();
    }

    infra::ByteRange NorOpenFile::Pending() const
    {
        return infra::Head(buffer, size - programmed);
    }

    std::optional<uint32_t> NorOpenFile::UncommittedBlock(uint32_t index) const
    {
        auto entry = std::ranges::find(entries, index, &NorBlockEntry::index);
        if (entry == entries.end())
            return std::nullopt;

        return entry->block;
    }

    std::optional<uint32_t> NorOpenFile::ForeignTailIndex(uint32_t payload) const
    {
        if (!tailBlock.has_value() || ownsTailBlock || programmed % payload == 0)
            return std::nullopt;

        return programmed / payload;
    }

    bool NorOpenFile::Admits(uint32_t index, uint32_t payload, uint32_t fanout) const
    {
        infra::BoundedVector<uint32_t>::WithMaxSize<norUncommittedBlockEntries + 2> indices;
        auto add = [&indices](uint32_t candidate)
        {
            if (std::ranges::find(indices, candidate) == indices.end())
                indices.push_back(candidate);
        };

        for (const auto& entry : entries)
            add(entry.index);
        add(index);
        if (auto tail = ForeignTailIndex(payload); tail.has_value())
            add(*tail);

        if (indices.size() > norUncommittedBlockEntries)
            return false;

        auto firstLeaf = indices.front() / fanout;
        std::optional<uint32_t> secondLeaf;
        for (auto candidate : indices)
        {
            auto leaf = candidate / fanout;
            if (leaf == firstLeaf || leaf == secondLeaf)
                continue;

            if (secondLeaf.has_value())
                return false;

            secondLeaf = leaf;
        }

        return true;
    }

    std::optional<uint32_t> NorOpenFile::SecondLeafIndex(uint32_t fanout) const
    {
        if (entries.empty())
            return std::nullopt;

        auto firstLeaf = entries.front().index / fanout;
        auto other = std::ranges::find_if(entries, [firstLeaf, fanout](const NorBlockEntry& entry)
            {
                return entry.index / fanout != firstLeaf;
            });
        if (other == entries.end())
            return std::nullopt;

        return other->index;
    }
}
