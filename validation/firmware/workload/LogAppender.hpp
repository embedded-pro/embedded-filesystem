#ifndef VALIDATION_FIRMWARE_WORKLOAD_LOG_APPENDER_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_LOG_APPENDER_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include "validation/firmware/workload/WorkloadRunner.hpp"
#include <array>
#include <chrono>
#include <cstdint>

namespace validation
{
    // AppendLog of metrics/workload/Workloads.cpp: appends records, each synced and followed by an idle period
    class LogAppender
    {
    public:
        static constexpr uint32_t appendSize = 64;
        static constexpr infra::Duration idlePeriod = std::chrono::milliseconds(100);

        explicit LogAppender(WorkloadRunner& runner);
        LogAppender(const LogAppender& other) = delete;
        LogAppender& operator=(const LogAppender& other) = delete;
        ~LogAppender();

        void Append(infra::BoundedConstString path, uint32_t fileId, uint32_t size, const infra::Function<void()>& onDone);

    private:
        void AppendNext();
        void Appended(fs::Result result);
        void Close();

    private:
        WorkloadRunner& runner;
        uint32_t fileId{ 0 };
        uint32_t size{ 0 };
        uint32_t offset{ 0 };
        infra::AutoResetFunction<void()> onDone;
        std::array<uint8_t, appendSize> record{};
    };
}

#endif
