#ifndef FS_NOR_NOR_HEADER_SCAN_HPP
#define FS_NOR_NOR_HEADER_SCAN_HPP

#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    // During the scan, the state in use marks a block whose erase count is not known; the checkpoint decides which blocks are in use
    constexpr NorBlockState norUnknownEraseCount = NorBlockState::inUse;

    struct NorScanSummary
    {
        uint64_t highestSequence{ 0 };
        std::optional<uint32_t> checkpointCandidate;
        uint64_t checkpointSequence{ 0 };
        uint64_t countSum{ 0 };
        uint32_t countsKnown{ 0 };

        uint32_t AverageCount() const;
    };

    class NorHeaderScan
    {
    public:
        NorHeaderScan(hal::Flash& flash, NorVolumeState& volume, infra::ByteRange buffer, NorScanSummary& summary, const infra::Function<void()>& onDone);
        NorHeaderScan(const NorHeaderScan& other) = delete;
        NorHeaderScan& operator=(const NorHeaderScan& other) = delete;
        ~NorHeaderScan() = default;

        void Start();

    private:
        void ReadHeader();
        void OnHeader();
        void Count(const NorBlockHeader& header) const;
        void Classify(const NorBlockHeader& header) const;

    private:
        hal::Flash& flash;
        NorVolumeState& volume;
        infra::ByteRange buffer;
        NorScanSummary& summary;
        uint32_t block{ 0 };
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
