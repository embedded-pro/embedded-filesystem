#ifndef FS_NOR_NOR_CONFIG_HPP
#define FS_NOR_NOR_CONFIG_HPP

#include "fs/nor/NorLayout.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>

namespace fs
{
    struct NorConfig
    {
        uint32_t maxBlocks{ 0 };
        uint32_t sectorsPerBlock{ 1 };
        uint32_t programUnit{ 1 };
        uint32_t pageSize{ 256 };
        uint32_t nodeSize{ 512 };
        std::size_t cachedNodes{ 16 };
        uint32_t inlineBytes{ 64 };
        uint32_t maxNameBytes{ 64 };
        std::size_t maxPathBytes{ 255 };
        std::size_t directories{ 1 };
        std::size_t pathOperations{ 2 };
        uint32_t journalBlocks{ 2 };
        uint32_t staticWearLevelingThreshold{ 64 };
        uint32_t staticWearLevelingInterval{ 32 };
    };

    struct NorFeatures
    {
        bool makeDirectory{ true };
        bool remove{ true };
        bool rename{ true };
        bool truncate{ true };
        bool format{ true };
        bool overwrite{ true };
    };

    constexpr std::size_t norMinimumCachedNodes = 15;

    constexpr uint32_t NorInodeRecordBytes(const NorConfig& config)
    {
        return NorInodeRecordBytes(config.inlineBytes, config.maxNameBytes);
    }

    constexpr uint32_t NorFileBufferBytes(const NorConfig& config)
    {
        return config.pageSize > config.inlineBytes ? config.pageSize : config.inlineBytes;
    }

    namespace detail
    {
        constexpr bool IsValidNorGeometry(const NorConfig& config)
        {
            return config.maxBlocks >= 4 && config.sectorsPerBlock != 0 && config.sectorsPerBlock <= 0xffff && std::has_single_bit(config.programUnit) && std::has_single_bit(config.pageSize) && config.pageSize >= config.programUnit;
        }

        constexpr bool IsValidNorNodes(const NorConfig& config)
        {
            return config.nodeSize % config.pageSize == 0 && config.nodeSize <= 0x8000 && config.cachedNodes >= norMinimumCachedNodes && NorKeyedEntries(config.nodeSize, NorInodeRecordBytes(config)) >= 2;
        }

        constexpr bool IsValidNorInodes(const NorConfig& config)
        {
            return config.inlineBytes >= NorHeaderSize(config.programUnit) && config.maxNameBytes != 0 && config.maxNameBytes <= 255 && config.maxPathBytes > config.maxNameBytes;
        }

        constexpr bool IsValidNorWork(const NorConfig& config)
        {
            return config.pathOperations != 0 && config.journalBlocks >= 2 && config.journalBlocks <= norMaxJournalBlocks && config.staticWearLevelingThreshold != 0 && config.staticWearLevelingThreshold <= 128 && config.staticWearLevelingInterval != 0;
        }
    }

    constexpr bool IsValidNorConfig(const NorConfig& config)
    {
        return detail::IsValidNorGeometry(config) && detail::IsValidNorNodes(config) && detail::IsValidNorInodes(config) && detail::IsValidNorWork(config);
    }
}

#endif
