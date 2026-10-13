#include "metrics/power_cut/PowerCutSweep.hpp"
#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "metrics/device/MeteredBlockDevice.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "metrics/device/VirtualTimeEnvironment.hpp"

namespace metrics
{
    namespace
    {
        std::string Name(NorCutMode mode)
        {
            switch (mode)
            {
                case NorCutMode::notStarted:
                    return "notStarted";
                case NorCutMode::partial:
                    return "partial";
                default:
                    return "completed";
            }
        }

        std::string Name(const BlockCut& cut)
        {
            std::string name = cut.mode == InterruptedBlock::unwritten ? "unwritten" : cut.mode == InterruptedBlock::written ? "written"
                                                                                                                             : "torn";
            if (cut.unflushed == UnflushedWrites::lost)
                return name + "+lossOfUnflushedWrites";

            return cut.unflushed == UnflushedWrites::reordered ? name + "+reorderedUnflushedWrites" : name;
        }

        void Record(SweepResult& result, std::optional<uint64_t> step, std::string mode, std::optional<std::string> reason)
        {
            if (reason)
                result.failures.push_back({ step, std::move(mode), *reason });
            else if (step)
                ++result.consistentRuns;
        }
    }

    double SweepResult::ConsistentShare() const
    {
        return runs == 0 ? 1.0 : static_cast<double>(consistentRuns) / runs;
    }

    std::vector<uint64_t> SampledCutPoints(uint64_t writeSteps, std::optional<uint64_t> maximum)
    {
        uint64_t stride = 1;
        if (maximum && *maximum != 0 && writeSteps > *maximum)
            stride = (writeSteps + *maximum - 1) / *maximum;

        std::vector<uint64_t> points;
        for (uint64_t step = 0; step < writeSteps; step += stride)
            points.push_back(step);

        return points;
    }

    NorPowerCutSweep::NorPowerCutSweep(const NorModel& model, std::vector<NorCutMode> modes)
        : model(model)
        , modes(std::move(modes))
        , flash(model.numberOfSectors, model.sectorSize)
    {}

    void NorPowerCutSweep::LimitCutPoints(uint64_t maximum)
    {
        maximumCutPoints = maximum;
    }

    SweepResult NorPowerCutSweep::Run(const Phase& setUp, const Phase& operation, const Verify& verify)
    {
        {
            VirtualTimeEnvironment environment;
            MeteredFlash metered{ flash, model };
            Context context{ environment.Time(), metered };
            setUp(context);
        }

        image = flash.sectors;

        SweepResult result;
        result.writeSteps = CountingRun(operation, verify, result);
        for (auto step : SampledCutPoints(result.writeSteps, maximumCutPoints))
            for (auto mode : modes)
                CutRun({ step, mode }, operation, verify, result);

        return result;
    }

    uint64_t NorPowerCutSweep::CountingRun(const Phase& operation, const Verify& verify, SweepResult& result)
    {
        flash.sectors = image;
        uint64_t writeSteps = 0;

        {
            VirtualTimeEnvironment environment;
            PowerCutFlash power{ flash };
            MeteredFlash metered{ power, model };
            Context context{ environment.Time(), metered };
            operation(context);
            writeSteps = power.WriteSteps();
        }

        Record(result, std::nullopt, "uncut", VerifyImage(verify));
        return writeSteps;
    }

    void NorPowerCutSweep::CutRun(const NorCut& cut, const Phase& operation, const Verify& verify, SweepResult& result)
    {
        flash.sectors = image;

        {
            VirtualTimeEnvironment environment;
            PowerCutFlash power{ flash };
            power.Arm(cut);
            MeteredFlash metered{ power, model };
            Context context{ environment.Time(), metered };
            operation(context);
        }

        ++result.runs;
        Record(result, cut.step, Name(cut.mode), VerifyImage(verify));
    }

    std::optional<std::string> NorPowerCutSweep::VerifyImage(const Verify& verify)
    {
        VirtualTimeEnvironment environment;
        MeteredFlash metered{ flash, model };
        Context context{ environment.Time(), metered };
        return verify(context);
    }

    BlockPowerCutSweep::BlockPowerCutSweep(const BlockDeviceModel& model, std::vector<BlockCut> modes)
        : model(model)
        , modes(std::move(modes))
        , storage(static_cast<std::size_t>(model.numberOfBlocks) * model.blockSize, 0)
    {}

    void BlockPowerCutSweep::LimitCutPoints(uint64_t maximum)
    {
        maximumCutPoints = maximum;
    }

    SweepResult BlockPowerCutSweep::Run(const Phase& setUp, const Phase& operation, const Verify& verify)
    {
        {
            VirtualTimeEnvironment environment;
            hal::BlockDeviceStub stub{ infra::MakeRange(storage), model.blockSize };
            MeteredBlockDevice metered{ stub, model };
            Context context{ environment.Time(), metered };
            setUp(context);
        }

        image = storage;

        SweepResult result;
        result.writeSteps = CountingRun(operation, verify, result);
        for (auto step : SampledCutPoints(result.writeSteps, maximumCutPoints))
            for (auto cut : modes)
            {
                cut.step = step;
                CutRun(cut, operation, verify, result);
            }

        return result;
    }

    uint64_t BlockPowerCutSweep::CountingRun(const Phase& operation, const Verify& verify, SweepResult& result)
    {
        storage = image;
        uint64_t writeSteps = 0;

        {
            VirtualTimeEnvironment environment;
            hal::BlockDeviceStub stub{ infra::MakeRange(storage), model.blockSize };
            PowerCutBlockDevice power{ stub, infra::MakeRange(storage) };
            MeteredBlockDevice metered{ power, model };
            Context context{ environment.Time(), metered };
            operation(context);
            writeSteps = power.WriteSteps();
        }

        Record(result, std::nullopt, "uncut", VerifyImage(verify));
        return writeSteps;
    }

    void BlockPowerCutSweep::CutRun(const BlockCut& cut, const Phase& operation, const Verify& verify, SweepResult& result)
    {
        storage = image;

        {
            VirtualTimeEnvironment environment;
            hal::BlockDeviceStub stub{ infra::MakeRange(storage), model.blockSize };
            PowerCutBlockDevice power{ stub, infra::MakeRange(storage) };
            power.Arm(cut);
            MeteredBlockDevice metered{ power, model };
            Context context{ environment.Time(), metered };
            operation(context);
        }

        ++result.runs;
        Record(result, cut.step, Name(cut), VerifyImage(verify));
    }

    std::optional<std::string> BlockPowerCutSweep::VerifyImage(const Verify& verify)
    {
        VirtualTimeEnvironment environment;
        hal::BlockDeviceStub stub{ infra::MakeRange(storage), model.blockSize };
        MeteredBlockDevice metered{ stub, model };
        Context context{ environment.Time(), metered };
        return verify(context);
    }
}
