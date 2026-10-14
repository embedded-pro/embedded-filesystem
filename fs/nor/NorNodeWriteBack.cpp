#include "fs/nor/NorNodeWriteBack.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <utility>

namespace fs
{
    namespace
    {
        std::pair<bool, uint8_t> WriteOrder(const NorNodeHeader& header)
        {
            return { header.map == NorMap::inodeTable, header.level };
        }
    }

    NorNodeWriteBack::NorNodeWriteBack(NorNodeCache& cache, uint32_t inodeRecordBytes)
        : cache(cache)
        , inodeRecordBytes(inodeRecordBytes)
    {}

    std::optional<std::size_t> NorNodeWriteBack::Next() const
    {
        std::optional<std::size_t> next;
        std::pair<bool, uint8_t> order;

        for (std::size_t slot = 0; slot != cache.Capacity(); ++slot)
            if (cache.IsModified(slot))
            {
                auto candidate = WriteOrder(ReadNorNodeHeader(cache.Node(slot)));
                if (!next.has_value() || candidate < order)
                {
                    next = slot;
                    order = candidate;
                }
            }

        return next;
    }

    infra::ConstByteRange NorNodeWriteBack::Seal(std::size_t slot) const
    {
        auto node = cache.Modify(slot);
        SealNorNode(ReadNorNodeHeader(node), node);
        return node;
    }

    void NorNodeWriteBack::Written(std::size_t slot, uint32_t address, NorMapRoot& inodeTable) const
    {
        auto from = cache.AddressOf(slot);
        auto header = ReadNorNodeHeader(cache.Node(slot));
        cache.Written(slot, address);

        if (PatchParent(header, from, address))
            return;

        if (header.map == NorMap::inodeTable)
        {
            really_assert(inodeTable.address == from);
            inodeTable.address = address;
        }
        else
            PatchInodeRecord(header.owner, from, address, inodeTable);
    }

    bool NorNodeWriteBack::PatchParent(const NorNodeHeader& child, uint32_t from, uint32_t to) const
    {
        for (std::size_t slot = 0; slot != cache.Capacity(); ++slot)
        {
            if (!cache.IsModified(slot))
                continue;

            auto header = ReadNorNodeHeader(cache.Node(slot));
            if (header.map != child.map || header.owner != child.owner || header.level != child.level + 1)
                continue;

            auto node = cache.Modify(slot);
            if (child.map == NorMap::fileData ? NorRadixTree::ReplaceChild(node, from, to) : NorBTree::ReplaceChild(node, from, to))
                return true;
        }

        return false;
    }

    void NorNodeWriteBack::PatchInodeRecord(uint32_t owner, uint32_t from, uint32_t to, NorMapRoot& inodeTable) const
    {
        NorBTree inodes{ cache, NorMapId{ NorMap::inodeTable, 0, inodeRecordBytes } };
        really_assert(inodes.Find(inodeTable, owner).has_value() && inodes.NodesModifiedByChange(inodeTable, owner) == 0);

        auto record = inodes.Modify(inodeTable, owner);
        really_assert(LoadLittle32(record, norInodeMapRootOffset) == from);
        StoreLittle32(record, norInodeMapRootOffset, to);
    }
}
