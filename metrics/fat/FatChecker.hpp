#ifndef METRICS_FAT_FAT_CHECKER_HPP
#define METRICS_FAT_FAT_CHECKER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace metrics
{
    using SectorReader = std::function<std::vector<uint8_t>(uint32_t sector)>;

    enum class FatCheckMode : uint8_t
    {
        afterCut,
        strict
    };

    struct FatImageLayout
    {
        uint32_t bits{ 0 };
        uint32_t volumeStart{ 0 };
        uint32_t sectorsPerCluster{ 0 };
        uint32_t firstFatSector{ 0 };
        uint32_t sectorsPerFat{ 0 };
        uint32_t fatCopies{ 0 };
        uint32_t rootSector{ 0 };
        uint32_t rootSectors{ 0 };
        uint32_t rootCluster{ 0 };
        uint32_t firstDataSector{ 0 };
        uint32_t clusterCount{ 0 };

        uint32_t SectorOfCluster(uint32_t cluster) const;
        uint32_t ClusterBytes() const;
    };

    struct FatImageEntry
    {
        std::string path;
        bool directory{ false };
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        std::vector<uint32_t> clusters;
    };

    struct FatCheck
    {
        std::optional<std::string> problem;
        FatImageLayout layout;
        std::vector<FatImageEntry> entries;
        uint32_t lostClusters{ 0 };
        uint32_t orphanedLongNames{ 0 };

        const FatImageEntry* Find(const std::string& path) const;
    };

    FatCheck CheckFatImage(const SectorReader& read, FatCheckMode mode);
    std::vector<uint8_t> ReadContents(const SectorReader& read, const FatCheck& check, const FatImageEntry& entry);
}

#endif
