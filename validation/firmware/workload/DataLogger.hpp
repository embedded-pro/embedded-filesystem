#ifndef VALIDATION_FIRMWARE_WORKLOAD_DATA_LOGGER_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_DATA_LOGGER_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/workload/LogAppender.hpp"
#include "validation/firmware/workload/VolumeFiller.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include <cstdint>

namespace validation
{
    // L2: appends of 64 B, each synced and followed by 100 ms of idle time, until a log of 4 MiB, scaled; then a second
    // log of as much, at most 128 KiB
    class DataLogger
        : public Workload
    {
    public:
        static constexpr uint32_t secondLogLimit = 128 * kibibyte;

        explicit DataLogger(WorkloadTools& tools);

        infra::BoundedConstString Name() const override;
        bool Fills() const override;
        void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) override;

    private:
        void FirstLog();
        void SecondLog();

    private:
        WorkloadTools& tools;
        LogAppender appender;
        uint32_t size{ 0 };
        uint32_t secondSize{ 0 };
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
