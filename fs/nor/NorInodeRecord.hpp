#ifndef FS_NOR_NOR_INODE_RECORD_HPP
#define FS_NOR_NOR_INODE_RECORD_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorConfig.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    enum class NorInodeType : uint8_t
    {
        file = 1,
        directory = 2
    };

    struct NorInodeRecord
    {
        uint32_t parent{ 0 };
        NorInodeType type{ NorInodeType::file };
        uint8_t mapHeight{ 0 };
        uint32_t size{ 0 };
        uint32_t mapRoot{ 0 };
        infra::ConstByteRange inlineBytes;
        infra::ConstByteRange name;

        bool operator==(const NorInodeRecord& other) const;
    };

    void SerializeNorInodeRecord(const NorInodeRecord& record, infra::ByteRange bytes, const NorConfig& config);
    std::optional<NorInodeRecord> ParseNorInodeRecord(infra::ConstByteRange bytes, const NorConfig& config);
    void AddNorInodePut(CrcRecord& record, uint32_t inode, const NorInodeRecord& inodeRecord, const NorConfig& config);
}

#endif
