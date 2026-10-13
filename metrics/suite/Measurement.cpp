#include "metrics/suite/Measurement.hpp"

namespace metrics
{
    namespace
    {
        constexpr std::string_view finished{ "finished" };
    }

    std::ostream& operator<<(std::ostream& stream, const NorSetup& setup)
    {
        return stream << setup.label;
    }

    std::ostream& operator<<(std::ostream& stream, const BlockSetup& setup)
    {
        return stream << setup.label;
    }

    NorMeasurement::NorMeasurement(const NorSetup& setup, TimingCase timingCase)
        : stub(setup.model->numberOfSectors, setup.model->sectorSize)
        , metered(stub, *setup.model, timingCase)
        , probe(metered)
        , subject(setup.create(environment.Time(), metered))
        , runner(environment.Time(), *subject, probe)
        , recorder(GlobalReport(), subject->Name(), setup.model->name, subject->RamBytes())
        , scaleDivisor(setup.scaleDivisor)
    {
        GlobalReport().AddDevice(*setup.model);
    }

    Runner& NorMeasurement::Workloads()
    {
        return runner;
    }

    MetricRecorder& NorMeasurement::Recorder()
    {
        return recorder;
    }

    const MeteredFlash& NorMeasurement::Flash() const
    {
        return metered;
    }

    WorkloadParameters NorMeasurement::Parameters(uint8_t fillPercent) const
    {
        return WorkloadParameters{ .scaleDivisor = scaleDivisor, .fillPercent = fillPercent };
    }

    BlockMeasurement::BlockMeasurement(const BlockSetup& setup, TimingCase timingCase)
        : storage(static_cast<std::size_t>(setup.model->numberOfBlocks) * setup.model->blockSize)
        , stub(infra::MakeRange(storage), setup.model->blockSize)
        , metered(stub, *setup.model, timingCase)
        , probe(metered)
        , subject(setup.create(environment.Time(), metered, *setup.model))
        , runner(environment.Time(), *subject, probe)
        , recorder(GlobalReport(), subject->Name(), setup.model->name, subject->RamBytes(), setup.targets)
        , phaseWrites(metered)
        , scaleDivisor(setup.scaleDivisor)
    {
        GlobalReport().AddDevice(*setup.model);
        runner.ObservePhases([this](const std::string& phase)
            {
                phaseWrites.PhaseEnded(phase);
            });
    }

    Runner& BlockMeasurement::Workloads()
    {
        return runner;
    }

    MetricRecorder& BlockMeasurement::Recorder()
    {
        return recorder;
    }

    const WritesOfPhase& BlockMeasurement::Writes(std::string_view phase)
    {
        runner.SetPhase(std::string{ finished });
        return phaseWrites.Of(phase);
    }

    WorkloadParameters BlockMeasurement::Parameters(uint8_t fillPercent) const
    {
        return WorkloadParameters{ .scaleDivisor = scaleDivisor, .fillPercent = fillPercent };
    }
}
