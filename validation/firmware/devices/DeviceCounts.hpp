#ifndef VALIDATION_FIRMWARE_DEVICES_DEVICE_COUNTS_HPP
#define VALIDATION_FIRMWARE_DEVICES_DEVICE_COUNTS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    struct FlashCounts
    {
        uint64_t readRequests{};
        uint64_t bytesRead{};
        uint64_t readNs{};
        uint64_t programRequests{};
        uint64_t bytesProgrammed{};
        uint64_t pagesProgrammed{};
        uint64_t programNs{};
        uint64_t eraseRequests{};
        uint64_t sectorsErased{};
        uint64_t eraseNs{};

        bool operator==(const FlashCounts& other) const = default;

        friend FlashCounts operator-(const FlashCounts& later, const FlashCounts& earlier)
        {
            return { later.readRequests - earlier.readRequests, later.bytesRead - earlier.bytesRead, later.readNs - earlier.readNs,
                later.programRequests - earlier.programRequests, later.bytesProgrammed - earlier.bytesProgrammed, later.pagesProgrammed - earlier.pagesProgrammed, later.programNs - earlier.programNs,
                later.eraseRequests - earlier.eraseRequests, later.sectorsErased - earlier.sectorsErased, later.eraseNs - earlier.eraseNs };
        }

        friend FlashCounts operator+(const FlashCounts& left, const FlashCounts& right)
        {
            return { left.readRequests + right.readRequests, left.bytesRead + right.bytesRead, left.readNs + right.readNs,
                left.programRequests + right.programRequests, left.bytesProgrammed + right.bytesProgrammed, left.pagesProgrammed + right.pagesProgrammed, left.programNs + right.programNs,
                left.eraseRequests + right.eraseRequests, left.sectorsErased + right.sectorsErased, left.eraseNs + right.eraseNs };
        }
    };

    struct BlockCounts
    {
        static constexpr std::size_t recentUnits = 4;

        uint64_t readRequests{};
        uint64_t blocksRead{};
        uint64_t readNs{};
        uint64_t writeRequests{};
        uint64_t blocksWritten{};
        uint64_t writeNs{};
        std::array<uint64_t, recentUnits> unitsByRecency{};
        uint64_t unitsNotRecent{};
        uint64_t alignedBlocks{};
        uint64_t distinctUnits{};
        uint64_t flushRequests{};
        uint64_t flushNs{};
        uint64_t discardRequests{};
        uint64_t blocksDiscarded{};
        uint64_t discardNs{};
        uint64_t failedRequests{};

        bool operator==(const BlockCounts& other) const = default;

        friend BlockCounts operator-(const BlockCounts& later, const BlockCounts& earlier)
        {
            return Combine(later, earlier, -1);
        }

        friend BlockCounts operator+(const BlockCounts& left, const BlockCounts& right)
        {
            return Combine(left, right, 1);
        }

    private:
        static BlockCounts Combine(const BlockCounts& left, const BlockCounts& right, int sign);
    };

    // The counts of a device of either kind, so that a workload runs on both alike; a device fills the counts of its kind
    struct DeviceCounts
    {
        FlashCounts flash;
        BlockCounts blocks;

        bool operator==(const DeviceCounts& other) const = default;

        friend DeviceCounts operator-(const DeviceCounts& later, const DeviceCounts& earlier)
        {
            return { later.flash - earlier.flash, later.blocks - earlier.blocks };
        }

        friend DeviceCounts operator+(const DeviceCounts& left, const DeviceCounts& right)
        {
            return { left.flash + right.flash, left.blocks + right.blocks };
        }
    };
}

#endif
