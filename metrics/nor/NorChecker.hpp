#ifndef METRICS_NOR_NOR_CHECKER_HPP
#define METRICS_NOR_NOR_CHECKER_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace metrics
{
    struct NorImageSuperblock
    {
        uint32_t sectorsPerBlock{ 0 };
        uint32_t sectorSize{ 0 };
        uint32_t blocks{ 0 };
        uint32_t programUnit{ 0 };
        uint32_t nodeSize{ 0 };
        uint32_t inlineBytes{ 0 };
        uint32_t maxNameBytes{ 0 };

        bool operator==(const NorImageSuperblock& other) const = default;
    };

    struct NorImageLayout
    {
        uint32_t blockSize{ 0 };
        uint32_t headerSize{ 0 };
        uint32_t payloadSize{ 0 };
        uint32_t nodeSlotsOffset{ 0 };
    };

    enum class NorBlockUse : uint8_t
    {
        superblock,
        checkpoint,
        journal,
        node,
        data,
        freeErased,
        free
    };

    struct NorImageEntry
    {
        std::string path;
        uint32_t inode{ 0 };
        bool directory{ false };
        uint32_t size{ 0 };
        std::map<uint32_t, uint32_t> blocks;
        std::vector<uint8_t> inlineBytes;
    };

    struct NorCheck
    {
        std::optional<std::string> problem;
        std::optional<NorImageSuperblock> superblock;
        uint32_t validSuperblocks{ 0 };
        NorImageLayout layout;
        std::vector<std::optional<uint32_t>> eraseCounts;
        std::vector<NorBlockUse> blocks;
        uint32_t records{ 0 };
        std::vector<NorImageEntry> entries;

        const NorImageEntry* Find(const std::string& path) const;
    };

    NorCheck CheckNorImage(const std::vector<uint8_t>& image, uint32_t sectorSize, uint32_t pageSize);
    std::vector<uint8_t> ReadContents(const std::vector<uint8_t>& image, const NorCheck& check, const NorImageEntry& entry);
}

#endif
