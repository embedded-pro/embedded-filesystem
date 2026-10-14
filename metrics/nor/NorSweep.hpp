#ifndef METRICS_NOR_NOR_SWEEP_HPP
#define METRICS_NOR_NOR_SWEEP_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/BoundedVector.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/nor/NorChecker.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace metrics
{
    extern const NorModel norSweep128k;
    extern const NorModel internalSweep96k;
    extern const NorModel norSweep128kTwoSectors;

    constexpr fs::NorConfig norSweep128kConfig{ .maxBlocks = 32, .cachedNodes = fs::norMinimumCachedNodes };
    constexpr fs::NorConfig internalSweep96kConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = fs::norMinimumCachedNodes };
    constexpr fs::NorConfig norSweep128kTwoSectorsConfig{ .maxBlocks = 32, .sectorsPerBlock = 2, .cachedNodes = fs::norMinimumCachedNodes };

    fs::NorConfig SweepConfig(const NorModel& model);
    std::size_t SweepRamBytes(const NorModel& model);
    NorImageSuperblock SuperblockOf(const NorModel& model, const fs::NorConfig& config);

    std::optional<fs::Result> Format(SweepContext<hal::Flash>& context, const fs::NorConfig& config);
    std::vector<uint8_t> ReadImage(SweepContext<hal::Flash>& context);
    NorCheck CheckImage(SweepContext<hal::Flash>& context, const NorModel& model);
    void Program(SweepContext<hal::Flash>& context, uint32_t address, const std::vector<uint8_t>& bytes);
    void EraseSector(SweepContext<hal::Flash>& context, uint32_t sector);

    namespace detail
    {
        struct NorSweepVolumeStorage
        {
            explicit NorSweepVolumeStorage(const fs::NorConfig& config);

            std::vector<uint8_t> blockStates;
            std::vector<uint8_t> eraseCounts;
            std::vector<uint8_t> journal;
            std::vector<uint8_t> nodes;
            std::vector<fs::NorNodeCache::Slot> nodeSlots;
            fs::DeferredCompletions::WithMaxSize<8> deferred;
        };
    }

    class NorSweepVolume
    {
    public:
        NorSweepVolume(SweepContext<hal::Flash>& context, const fs::NorConfig& config);

        fs::Result Format();
        fs::Result Mount();
        fs::Result Unmount();

    private:
        class FileSystem
            : private detail::NorSweepVolumeStorage
            , public fs::NorFileSystem
        {
        public:
            FileSystem(hal::Flash& flash, const fs::NorConfig& config);

        private:
            infra::SharedPtr<fs::NorDirectory> AllocateDirectory() override;

        private:
            infra::BoundedVector<fs::NorPathSlot>::WithMaxSize<8> pathSlots;
            fs::HandlePool<fs::NorDirectory, 1> directories{ Volume() };
        };

        template<class Start>
        fs::Result Run(const Start& start);

    private:
        SweepContext<hal::Flash>& context;
        FileSystem fileSystem;
    };
}

#endif
