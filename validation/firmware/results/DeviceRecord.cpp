#include "validation/firmware/results/DeviceRecord.hpp"

namespace validation
{
    void PrintDevice(ResultLines& results, const DeviceDescription& device)
    {
        const bool nor = device.kind == DeviceKind::nor;
        auto record = results.Record("device");
        record.Add("device", device.name)
            .Add("kind", nor ? "nor" : "block")
            .Add(nor ? "sectors" : "blocks", device.units)
            .Add(nor ? "sectorSize" : "blockSize", device.unitBytes);

        if (device.pageBytes != 0)
            record.Add("pageSize", device.pageBytes);

        if (!device.chip.empty())
            record.Add("chip", device.chip);

        if (device.clockHz != 0)
            record.Add("clockHz", device.clockHz);

        if (!device.memory.empty())
            record.Add("memory", device.memory);
    }
}
