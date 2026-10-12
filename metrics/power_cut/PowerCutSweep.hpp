#ifndef METRICS_POWER_CUT_POWER_CUT_SWEEP_HPP
#define METRICS_POWER_CUT_POWER_CUT_SWEEP_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "hal/interfaces/Flash.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/device/VirtualTime.hpp"
#include "metrics/power_cut/PowerCutBlockDevice.hpp"
#include "metrics/power_cut/PowerCutFlash.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace metrics
{
    struct SweepFailure
    {
        std::optional<uint64_t> step;
        std::string mode;
        std::string reason;
    };

    struct SweepResult
    {
        uint64_t writeSteps{};
        uint64_t runs{};
        uint64_t consistentRuns{};
        std::vector<SweepFailure> failures;

        double ConsistentShare() const;
    };

    template<class Device>
    struct SweepContext
    {
        VirtualTime& time;
        Device& device;
    };

    class NorPowerCutSweep
    {
    public:
        using Context = SweepContext<hal::Flash>;
        using Phase = std::function<void(Context&)>;
        using Verify = std::function<std::optional<std::string>(Context&)>;

        explicit NorPowerCutSweep(const NorModel& model, std::vector<NorCutMode> modes = { NorCutMode::notStarted, NorCutMode::partial, NorCutMode::completed });

        void LimitCutPoints(uint64_t maximum);
        SweepResult Run(const Phase& setUp, const Phase& operation, const Verify& verify);

    private:
        uint64_t CountingRun(const Phase& operation, const Verify& verify, SweepResult& result);
        void CutRun(const NorCut& cut, const Phase& operation, const Verify& verify, SweepResult& result);
        std::optional<std::string> VerifyImage(const Verify& verify);

    private:
        const NorModel& model;
        std::vector<NorCutMode> modes;
        std::optional<uint64_t> maximumCutPoints;
        hal::FlashStub flash;
        std::vector<std::vector<uint8_t>> image;
    };

    class BlockPowerCutSweep
    {
    public:
        using Context = SweepContext<hal::BlockDevice>;
        using Phase = std::function<void(Context&)>;
        using Verify = std::function<std::optional<std::string>(Context&)>;

        explicit BlockPowerCutSweep(const BlockDeviceModel& model, std::vector<BlockCut> modes = { { 0, InterruptedBlock::unwritten }, { 0, InterruptedBlock::written }, { 0, InterruptedBlock::torn } });

        void LimitCutPoints(uint64_t maximum);
        SweepResult Run(const Phase& setUp, const Phase& operation, const Verify& verify);

    private:
        uint64_t CountingRun(const Phase& operation, const Verify& verify, SweepResult& result);
        void CutRun(const BlockCut& cut, const Phase& operation, const Verify& verify, SweepResult& result);
        std::optional<std::string> VerifyImage(const Verify& verify);

    private:
        const BlockDeviceModel& model;
        std::vector<BlockCut> modes;
        std::optional<uint64_t> maximumCutPoints;
        std::vector<uint8_t> storage;
        std::vector<uint8_t> image;
    };

    std::vector<uint64_t> SampledCutPoints(uint64_t writeSteps, std::optional<uint64_t> maximum);
}

#endif
