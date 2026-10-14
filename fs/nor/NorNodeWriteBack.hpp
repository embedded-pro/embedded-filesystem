#ifndef FS_NOR_NOR_NODE_WRITE_BACK_HPP
#define FS_NOR_NOR_NODE_WRITE_BACK_HPP

#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorNodeWriteBack
    {
    public:
        NorNodeWriteBack(NorNodeCache& cache, uint32_t inodeRecordBytes);

        std::optional<std::size_t> Next() const;
        infra::ConstByteRange Seal(std::size_t slot) const;
        void Written(std::size_t slot, uint32_t address, NorMapRoot& inodeTable) const;

    private:
        bool PatchParent(const NorNodeHeader& child, uint32_t from, uint32_t to) const;
        void PatchInodeRecord(uint32_t owner, uint32_t from, uint32_t to, NorMapRoot& inodeTable) const;

    private:
        NorNodeCache& cache;
        uint32_t inodeRecordBytes;
    };
}

#endif
