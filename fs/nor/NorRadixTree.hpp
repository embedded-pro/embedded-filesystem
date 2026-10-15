#ifndef FS_NOR_NOR_RADIX_TREE_HPP
#define FS_NOR_NOR_RADIX_TREE_HPP

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
    class NorRadixTree
    {
    public:
        NorRadixTree(NorNodeCache& cache, uint32_t owner);

        uint32_t Find(const NorMapRoot& root, uint32_t index) const;
        std::optional<uint32_t> NodesModifiedBySet(const NorMapRoot& root, uint32_t index) const;
        std::optional<uint32_t> NodesCreatedBySet(const NorMapRoot& root, uint32_t index) const;
        uint32_t NodesModifiedByClear(const NorMapRoot& root, uint32_t index) const;

        void Set(NorMapRoot& root, uint32_t index, uint32_t block) const;
        void Clear(NorMapRoot& root, uint32_t index) const;

        static bool Covers(const NorMapRoot& root, uint32_t index, uint32_t nodeSize);
        static bool CanMap(uint32_t index, uint32_t nodeSize);
        static uint32_t ChildFor(infra::ConstByteRange node, uint32_t index);
        static uint32_t FirstIndexOfChild(infra::ConstByteRange node, uint32_t index);
        static bool HasValidCount(infra::ConstByteRange node);
        static bool ReplaceChild(infra::ByteRange node, uint32_t from, uint32_t to);

    private:
        struct Path
        {
            std::array<std::size_t, norMaxLevels> slots{};
            std::array<uint32_t, norMaxLevels> positions{};
            std::size_t nodes{ 0 };
            bool complete{ false };
        };

        Path Walk(const NorMapRoot& root, uint32_t index) const;
        uint32_t Leaf(const Path& path) const;
        bool Maps(const Path& path) const;
        uint32_t UnmodifiedNodes(const Path& path) const;
        std::optional<uint8_t> HeightFor(uint32_t index) const;

    private:
        NorNodeCache& cache;
        uint32_t owner;
    };
}

#endif
