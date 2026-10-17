#ifndef VALIDATION_FIRMWARE_WORKLOAD_VOLUME_FILLER_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_VOLUME_FILLER_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/workload/FileWriter.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include "validation/firmware/workload/WorkloadRunner.hpp"
#include <cstdint>

namespace validation
{
    // FillVolume of metrics/workload/Workloads.cpp: writes files into /fill until the volume holds a share of what the
    // workload does not reserve
    class VolumeFiller
    {
    public:
        VolumeFiller(WorkloadRunner& runner, FileWriter& writer);
        VolumeFiller(const VolumeFiller& other) = delete;
        VolumeFiller& operator=(const VolumeFiller& other) = delete;
        ~VolumeFiller();

        void Fill(uint32_t percent, const WorkloadParameters& parameters, uint64_t reservedBytes, const infra::Function<void()>& onDone);

    private:
        void Measured(fs::Result result, fs::VolumeUsage usage);
        void Continue(fs::Result result, fs::VolumeUsage usage);
        void Written(bool written);

    private:
        WorkloadRunner& runner;
        FileWriter& writer;
        uint32_t percent{ 0 };
        uint32_t fileSize{ 0 };
        uint64_t reservedBytes{ 0 };
        uint64_t target{ 0 };
        uint32_t index{ 0 };
        WorkloadPath path;
        infra::AutoResetFunction<void()> onDone;
    };

    // Prepare of metrics/workload/Workloads.cpp: formats and mounts the volume, and fills it when the workload runs at a
    // fill level
    class Preparation
    {
    public:
        Preparation(WorkloadRunner& runner, VolumeFiller& filler);
        Preparation(const Preparation& other) = delete;
        Preparation& operator=(const Preparation& other) = delete;
        ~Preparation();

        void Prepare(infra::BoundedConstString workload, const WorkloadParameters& parameters, uint64_t reservedBytes, const infra::Function<void()>& onDone);

    private:
        void Mounted();

    private:
        WorkloadRunner& runner;
        VolumeFiller& filler;
        WorkloadParameters parameters;
        uint64_t reservedBytes{ 0 };
        infra::AutoResetFunction<void()> onDone;
    };

    struct WorkloadTools
    {
        explicit WorkloadTools(WorkloadRunner& runner);
        WorkloadTools(const WorkloadTools& other) = delete;
        WorkloadTools& operator=(const WorkloadTools& other) = delete;
        ~WorkloadTools();

        WorkloadRunner& runner;
        FileWriter writer;
        VolumeFiller filler;
        Preparation preparation;
    };
}

#endif
