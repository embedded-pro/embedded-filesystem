#ifndef METRICS_WORKLOAD_DEVICE_PROBE_HPP
#define METRICS_WORKLOAD_DEVICE_PROBE_HPP

#include "metrics/device/MeteredBlockDevice.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include <cstdint>
#include <string_view>

namespace metrics
{
    struct DeviceSnapshot
    {
        uint64_t readRequests{};
        uint64_t writeRequests{};
        uint64_t eraseRequests{};
        uint64_t flushRequests{};
        uint64_t discardRequests{};
        uint64_t bytesRead{};
        uint64_t bytesWritten{};
        infra::Duration deviceTime{};
        infra::Duration eraseTime{};

        DeviceSnapshot operator-(const DeviceSnapshot& earlier) const;
        DeviceSnapshot operator+(const DeviceSnapshot& other) const;
        bool operator==(const DeviceSnapshot& other) const = default;
    };

    class DeviceProbe
    {
    protected:
        DeviceProbe() = default;
        DeviceProbe(const DeviceProbe& other) = delete;
        DeviceProbe& operator=(const DeviceProbe& other) = delete;
        ~DeviceProbe() = default;

    public:
        virtual DeviceSnapshot Snapshot() const = 0;
        virtual uint64_t CapacityBytes() const = 0;
        virtual std::string_view ModelName() const = 0;
        virtual infra::Duration TypicalSectorErase() const = 0;
    };

    class MeteredFlashProbe
        : public DeviceProbe
    {
    public:
        explicit MeteredFlashProbe(const MeteredFlash& flash);

        DeviceSnapshot Snapshot() const override;
        uint64_t CapacityBytes() const override;
        std::string_view ModelName() const override;
        infra::Duration TypicalSectorErase() const override;

    private:
        const MeteredFlash& flash;
    };

    class MeteredBlockDeviceProbe
        : public DeviceProbe
    {
    public:
        explicit MeteredBlockDeviceProbe(const MeteredBlockDevice& device);

        DeviceSnapshot Snapshot() const override;
        uint64_t CapacityBytes() const override;
        std::string_view ModelName() const override;
        infra::Duration TypicalSectorErase() const override;

    private:
        const MeteredBlockDevice& device;
    };
}

#endif
