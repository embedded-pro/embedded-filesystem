#ifndef FS_NOR_NOR_B_TREE_HPP
#define FS_NOR_NOR_B_TREE_HPP

#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "infra/util/ByteRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorBTree
    {
    public:
        NorBTree(NorNodeCache& cache, const NorMapId& map);

        std::optional<infra::ConstByteRange> Find(const NorMapRoot& root, uint32_t key) const;
        std::optional<uint32_t> NodesModifiedByInsert(const NorMapRoot& root, uint32_t key) const;
        uint32_t NodesModifiedByChange(const NorMapRoot& root, uint32_t key) const;

        infra::ByteRange Insert(NorMapRoot& root, uint32_t key) const;
        infra::ByteRange Modify(NorMapRoot& root, uint32_t key) const;
        void Remove(NorMapRoot& root, uint32_t key) const;

        static uint32_t ChildFor(infra::ConstByteRange node, uint32_t key);
        static bool HasValidCount(infra::ConstByteRange node, uint32_t valueBytes);
        static bool ReplaceChild(infra::ByteRange node, uint32_t from, uint32_t to);

    private:
        struct Path
        {
            std::array<std::size_t, norMaxLevels> slots{};
            std::array<uint16_t, norMaxLevels> positions{};
            std::size_t nodes{ 0 };
        };

        uint32_t EntryBytes(uint8_t level) const;
        uint32_t Capacity(uint8_t level) const;
        Path Walk(const NorMapRoot& root, uint32_t key) const;
        bool Holds(const Path& path, uint32_t key) const;
        uint32_t UnmodifiedNodes(const Path& path) const;
        void ModifyPath(NorMapRoot& root, const Path& path) const;
        void LowerFirstKeys(const Path& path, uint32_t key) const;
        infra::ByteRange InsertEntry(std::size_t slot, uint16_t position, uint32_t key) const;
        void RemoveEntry(std::size_t slot, uint16_t position) const;
        void MoveTail(std::size_t from, std::size_t to, uint16_t entries) const;

    private:
        NorNodeCache& cache;
        NorMapId map;
    };
}

#endif
