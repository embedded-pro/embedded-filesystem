#include "validation/firmware/workload/VolumeFiller.hpp"
#include <algorithm>

namespace validation
{
    VolumeFiller::VolumeFiller(WorkloadRunner& runner, FileWriter& writer)
        : runner(runner)
        , writer(writer)
    {}

    VolumeFiller::~VolumeFiller() = default;

    void VolumeFiller::Fill(uint32_t percent, const WorkloadParameters& parameters, uint64_t reservedBytes, const infra::Function<void()>& onDone)
    {
        this->percent = percent;
        this->reservedBytes = reservedBytes;
        this->onDone = onDone;
        fileSize = Scaled(64 * kibibyte, parameters);
        index = 0;

        runner.SetPhase("fill");
        runner.MakeDirectory("/fill", [this](fs::Result)
            {
                runner.Usage([this](fs::Result result, fs::VolumeUsage usage)
                    {
                        Measured(result, usage);
                    });
            });
    }

    void VolumeFiller::Measured(fs::Result result, fs::VolumeUsage usage)
    {
        target = (usage.totalBytes - std::min(usage.totalBytes, reservedBytes)) * percent / 100;
        Continue(result, usage);
    }

    void VolumeFiller::Continue(fs::Result result, fs::VolumeUsage usage)
    {
        if (result != fs::Result::success || usage.usedBytes + fileSize > target)
        {
            onDone();
            return;
        }

        path = MakePath("/fill", index);
        writer.Write({ path, 1000 + index, fileSize, std::min(fileSize, chunkSize) }, [this](bool written)
            {
                Written(written);
            });
    }

    void VolumeFiller::Written(bool written)
    {
        if (!written)
        {
            onDone();
            return;
        }

        ++index;
        runner.Usage([this](fs::Result result, fs::VolumeUsage usage)
            {
                Continue(result, usage);
            });
    }

    Preparation::Preparation(WorkloadRunner& runner, VolumeFiller& filler)
        : runner(runner)
        , filler(filler)
    {}

    Preparation::~Preparation() = default;

    void Preparation::Prepare(infra::BoundedConstString workload, const WorkloadParameters& parameters, uint64_t reservedBytes, const infra::Function<void()>& onDone)
    {
        this->parameters = parameters;
        this->reservedBytes = reservedBytes;
        this->onDone = onDone;

        PhaseName phase{ workload };
        phase.append(",prepare");
        runner.SetPhase(phase);
        runner.Format([this](fs::Result)
            {
                runner.Mount([this](fs::Result)
                    {
                        Mounted();
                    });
            });
    }

    void Preparation::Mounted()
    {
        if (parameters.fillPercent == 0)
            onDone();
        else
            filler.Fill(parameters.fillPercent, parameters, reservedBytes, [this]
                {
                    onDone();
                });
    }

    WorkloadTools::WorkloadTools(WorkloadRunner& runner)
        : runner(runner)
        , writer(runner)
        , filler(runner, writer)
        , preparation(runner, filler)
    {}

    WorkloadTools::~WorkloadTools() = default;
}
