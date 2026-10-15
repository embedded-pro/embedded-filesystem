#include "metrics/workload/DeviceProbe.hpp"

namespace metrics
{
    DeviceSnapshot DeviceSnapshot::operator-(const DeviceSnapshot& earlier) const
    {
        return DeviceSnapshot{
            readRequests - earlier.readRequests,
            writeRequests - earlier.writeRequests,
            eraseRequests - earlier.eraseRequests,
            flushRequests - earlier.flushRequests,
            discardRequests - earlier.discardRequests,
            bytesRead - earlier.bytesRead,
            bytesWritten - earlier.bytesWritten,
            deviceTime - earlier.deviceTime,
            eraseTime - earlier.eraseTime,
        };
    }

    DeviceSnapshot DeviceSnapshot::operator+(const DeviceSnapshot& other) const
    {
        return DeviceSnapshot{
            readRequests + other.readRequests,
            writeRequests + other.writeRequests,
            eraseRequests + other.eraseRequests,
            flushRequests + other.flushRequests,
            discardRequests + other.discardRequests,
            bytesRead + other.bytesRead,
            bytesWritten + other.bytesWritten,
            deviceTime + other.deviceTime,
            eraseTime + other.eraseTime,
        };
    }

    MeteredFlashProbe::MeteredFlashProbe(const MeteredFlash& flash)
        : flash(flash)
    {}

    DeviceSnapshot MeteredFlashProbe::Snapshot() const
    {
        const auto& counters = flash.Counters();
        return DeviceSnapshot{ counters.readRequests, counters.programRequests, counters.eraseRequests, 0, 0, counters.bytesRead, counters.bytesProgrammed, counters.DeviceTime(), counters.eraseTime };
    }

    uint64_t MeteredFlashProbe::CapacityBytes() const
    {
        return flash.TotalSize();
    }

    std::string_view MeteredFlashProbe::ModelName() const
    {
        return flash.Model().name;
    }

    infra::Duration MeteredFlashProbe::TypicalSectorErase() const
    {
        return flash.Model().typical.sectorErase;
    }

    MeteredBlockDeviceProbe::MeteredBlockDeviceProbe(const MeteredBlockDevice& device)
        : device(device)
    {}

    DeviceSnapshot MeteredBlockDeviceProbe::Snapshot() const
    {
        const auto& counters = device.Counters();
        auto blockSize = device.BlockSize();
        return DeviceSnapshot{ counters.readRequests, counters.writeRequests, 0, counters.flushRequests, counters.discardRequests,
            counters.blocksRead * blockSize, counters.blocksWritten * blockSize, counters.DeviceTime() };
    }

    uint64_t MeteredBlockDeviceProbe::CapacityBytes() const
    {
        return static_cast<uint64_t>(device.NumberOfBlocks()) * device.BlockSize();
    }

    std::string_view MeteredBlockDeviceProbe::ModelName() const
    {
        return device.Model().name;
    }

    infra::Duration MeteredBlockDeviceProbe::TypicalSectorErase() const
    {
        return infra::Duration{};
    }
}
