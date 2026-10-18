#include "validation/firmware/workload/CountRecords.hpp"

namespace validation
{
    namespace
    {
        void AddFlashCounts(ResultRecord& record, const FlashCounts& counts)
        {
            record.Add("readRequests", counts.readRequests)
                .Add("bytesRead", counts.bytesRead)
                .Add("readNs", counts.readNs)
                .Add("programRequests", counts.programRequests)
                .Add("bytesProgrammed", counts.bytesProgrammed)
                .Add("pagesProgrammed", counts.pagesProgrammed)
                .Add("programNs", counts.programNs)
                .Add("eraseRequests", counts.eraseRequests)
                .Add("sectorsErased", counts.sectorsErased)
                .Add("eraseNs", counts.eraseNs);
        }

        void AddBlockCounts(ResultRecord& record, const BlockCounts& counts)
        {
            record.Add("readRequests", counts.readRequests)
                .Add("blocksRead", counts.blocksRead)
                .Add("readNs", counts.readNs)
                .Add("writeRequests", counts.writeRequests)
                .Add("blocksWritten", counts.blocksWritten)
                .Add("writeNs", counts.writeNs)
                .Add("unitsRecent1", counts.unitsByRecency[0])
                .Add("unitsRecent2", counts.unitsByRecency[1])
                .Add("unitsRecent3", counts.unitsByRecency[2])
                .Add("unitsRecent4", counts.unitsByRecency[3])
                .Add("unitsNotRecent", counts.unitsNotRecent)
                .Add("alignedBlocks", counts.alignedBlocks)
                .Add("distinctUnits", counts.distinctUnits)
                .Add("flushRequests", counts.flushRequests)
                .Add("flushNs", counts.flushNs)
                .Add("discardRequests", counts.discardRequests)
                .Add("blocksDiscarded", counts.blocksDiscarded)
                .Add("discardNs", counts.discardNs)
                .Add("failedRequests", counts.failedRequests);
        }
    }

    void AddCounts(ResultRecord& record, const DeviceCounts& counts, DeviceKind kind)
    {
        if (kind == DeviceKind::nor)
            AddFlashCounts(record, counts.flash);
        else
            AddBlockCounts(record, counts.blocks);
    }

    void AddCounts(ResultRecord& record, const VolumeCounts& counts)
    {
        record.Add("allocatedSectors", counts.allocatedSectors)
            .Add("erasedBlocks", counts.erasedBlocks)
            .Add("checkpoints", counts.checkpoints)
            .Add("movedBlocks", counts.movedBlocks);
    }
}
