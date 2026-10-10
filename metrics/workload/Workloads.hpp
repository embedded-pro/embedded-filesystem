#ifndef METRICS_WORKLOAD_WORKLOADS_HPP
#define METRICS_WORKLOAD_WORKLOADS_HPP

#include "metrics/workload/Runner.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace metrics
{
    struct WorkloadParameters
    {
        uint64_t seed{ 0x5eedf11e };
        uint32_t scaleDivisor{ 1 };
        uint8_t fillPercent{ 0 };
    };

    constexpr uint32_t appendSize = 64;
    constexpr uint32_t overwriteSize = 512;
    constexpr uint32_t randomReadSize = 512;

    std::string Phase(std::string_view workload, uint64_t value);
    std::string Phase(std::string_view workload, uint64_t first, uint64_t second);

    uint64_t FillVolume(Runner& runner, uint8_t percent, const WorkloadParameters& parameters, uint64_t reservedBytes);

    void SequentialWrite(Runner& runner, const WorkloadParameters& parameters);
    void DataLogger(Runner& runner, const WorkloadParameters& parameters);
    void RandomOverwrite(Runner& runner, const WorkloadParameters& parameters);
    void SmallFiles(Runner& runner, const WorkloadParameters& parameters);
    void StaticFill(Runner& runner, const WorkloadParameters& parameters, const std::function<bool(uint32_t rewrites)>& keepRewriting);
    void ReadPatterns(Runner& runner, const WorkloadParameters& parameters);
    void MountScaling(Runner& runner, const WorkloadParameters& parameters);
}

#endif
