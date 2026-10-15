#ifndef METRICS_SUITE_MEASUREMENT_HPP
#define METRICS_SUITE_MEASUREMENT_HPP

#include "hal/interfaces/test_doubles/BlockDeviceStub.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "metrics/device/MeteredBlockDevice.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "metrics/device/VirtualTimeEnvironment.hpp"
#include "metrics/suite/MetricRecorder.hpp"
#include "metrics/suite/PhaseWrites.hpp"
#include "metrics/workload/DeviceProbe.hpp"
#include "metrics/workload/Runner.hpp"
#include "metrics/workload/Workloads.hpp"
#include "gtest/gtest.h"
#include <functional>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace metrics
{
    struct NorSetup
    {
        std::string label;
        const NorModel* model;
        uint32_t scaleDivisor;
        std::function<std::shared_ptr<FileSystemUnderTest>(VirtualTime& time, hal::Flash& flash)> create;
        std::set<std::string, std::less<>> deferredWorkloads;
        bool diagnostic{ false };
    };

    struct BlockSetup
    {
        std::string label;
        const BlockDeviceModel* model;
        uint32_t scaleDivisor;
        std::function<std::shared_ptr<FileSystemUnderTest>(VirtualTime& time, hal::BlockDevice& device, const BlockDeviceModel& model)> create;
        const Targets* targets{ nullptr };
    };

    std::ostream& operator<<(std::ostream& stream, const NorSetup& setup);
    std::ostream& operator<<(std::ostream& stream, const BlockSetup& setup);

    template<class Setup>
    std::string SetupName(const testing::TestParamInfo<Setup>& info)
    {
        return info.param.label;
    }

    class NorMeasurement
    {
    public:
        explicit NorMeasurement(const NorSetup& setup, TimingCase timingCase = TimingCase::typical);
        NorMeasurement(const NorMeasurement& other) = delete;
        NorMeasurement& operator=(const NorMeasurement& other) = delete;

        Runner& Workloads();
        MetricRecorder& Recorder();
        const MeteredFlash& Flash() const;
        WorkloadParameters Parameters(uint8_t fillPercent = 0) const;

    private:
        VirtualTimeEnvironment environment;
        hal::FlashStub stub;
        MeteredFlash metered;
        MeteredFlashProbe probe;
        std::shared_ptr<FileSystemUnderTest> subject;
        Runner runner;
        MetricRecorder recorder;
        uint32_t scaleDivisor;
    };

    class BlockMeasurement
    {
    public:
        explicit BlockMeasurement(const BlockSetup& setup, TimingCase timingCase = TimingCase::typical);
        BlockMeasurement(const BlockMeasurement& other) = delete;
        BlockMeasurement& operator=(const BlockMeasurement& other) = delete;

        Runner& Workloads();
        MetricRecorder& Recorder();
        const WritesOfPhase& Writes(std::string_view phase);
        WorkloadParameters Parameters(uint8_t fillPercent = 0) const;

    private:
        VirtualTimeEnvironment environment;
        std::vector<uint8_t> storage;
        hal::BlockDeviceStub stub;
        MeteredBlockDevice metered;
        MeteredBlockDeviceProbe probe;
        std::shared_ptr<FileSystemUnderTest> subject;
        Runner runner;
        MetricRecorder recorder;
        PhaseWrites phaseWrites;
        uint32_t scaleDivisor;
    };
}

#endif
