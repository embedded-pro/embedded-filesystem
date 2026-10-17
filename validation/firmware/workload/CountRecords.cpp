#include "validation/firmware/workload/CountRecords.hpp"

namespace validation
{
    void AddCounts(ResultRecord& record, const FlashCounts& counts)
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

    void AddCounts(ResultRecord& record, const VolumeCounts& counts)
    {
        record.Add("allocatedSectors", counts.allocatedSectors)
            .Add("erasedBlocks", counts.erasedBlocks)
            .Add("checkpoints", counts.checkpoints)
            .Add("movedBlocks", counts.movedBlocks);
    }
}
