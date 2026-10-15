#ifndef FS_NOR_NOR_OPEN_FILE_HPP
#define FS_NOR_NOR_OPEN_FILE_HPP

#include "fs/interfaces/OpenMode.hpp"
#include "fs/nor/NorRecord.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorBlockEntry
    {
        uint32_t index{ 0 };
        uint32_t previous{ 0 };
        uint32_t block{ 0 };

        bool operator==(const NorBlockEntry& other) const = default;
    };

    struct NorOpenFile
    {
        uint32_t inode{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        OpenMode mode{ OpenMode::read };
        uint32_t committedSize{ 0 };
        uint32_t programmed{ 0 };
        std::optional<uint32_t> tailBlock;
        bool ownsTailBlock{ false };
        infra::BoundedVector<NorBlockEntry>::WithMaxSize<norUncommittedBlockEntries> entries;
        infra::ByteRange buffer;

        bool Writable() const;
        bool Uncommitted() const;
        infra::ByteRange Pending() const;
        std::optional<uint32_t> UncommittedBlock(uint32_t index) const;
        std::optional<uint32_t> ForeignTailIndex(uint32_t payload) const;
        bool Admits(uint32_t index, uint32_t payload, uint32_t fanout) const;
        std::optional<uint32_t> SecondLeafIndex(uint32_t fanout) const;
    };
}

#endif
