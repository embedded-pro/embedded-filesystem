#include "metrics/device/BlockDeviceStatistics.hpp"

namespace metrics
{
    infra::Duration BlockDeviceCounters::DeviceTime() const
    {
        return readTime + writeTime + discardTime + flushTime;
    }

    double BlockDeviceCounters::MeanBlocksPerWrite() const
    {
        return writeRequests == 0 ? 0.0 : static_cast<double>(blocksWritten) / writeRequests;
    }

    double BlockDeviceCounters::AlignedShare() const
    {
        return writeRequests == 0 ? 0.0 : static_cast<double>(alignedWrites) / writeRequests;
    }

    BlockDeviceCounters BlockDeviceCounters::operator-(const BlockDeviceCounters& earlier) const
    {
        return BlockDeviceCounters{
            readRequests - earlier.readRequests,
            writeRequests - earlier.writeRequests,
            discardRequests - earlier.discardRequests,
            flushRequests - earlier.flushRequests,
            failedRequests - earlier.failedRequests,
            blocksRead - earlier.blocksRead,
            blocksWritten - earlier.blocksWritten,
            blocksDiscarded - earlier.blocksDiscarded,
            alignedWrites - earlier.alignedWrites,
            allocationUnitOpenings - earlier.allocationUnitOpenings,
            readTime - earlier.readTime,
            writeTime - earlier.writeTime,
            discardTime - earlier.discardTime,
            flushTime - earlier.flushTime,
        };
    }
}
