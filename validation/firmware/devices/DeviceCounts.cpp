#include "validation/firmware/devices/DeviceCounts.hpp"
#include <algorithm>

namespace validation
{
    BlockCounts BlockCounts::Combine(const BlockCounts& left, const BlockCounts& right, int sign)
    {
        auto combine = [sign](uint64_t a, uint64_t b)
        {
            return sign > 0 ? a + b : a - b;
        };

        BlockCounts result{ combine(left.readRequests, right.readRequests), combine(left.blocksRead, right.blocksRead), combine(left.readNs, right.readNs),
            combine(left.writeRequests, right.writeRequests), combine(left.blocksWritten, right.blocksWritten), combine(left.writeNs, right.writeNs), {},
            combine(left.unitsNotRecent, right.unitsNotRecent), combine(left.alignedBlocks, right.alignedBlocks), combine(left.distinctUnits, right.distinctUnits),
            combine(left.flushRequests, right.flushRequests), combine(left.flushNs, right.flushNs), combine(left.discardRequests, right.discardRequests),
            combine(left.blocksDiscarded, right.blocksDiscarded), combine(left.discardNs, right.discardNs), combine(left.failedRequests, right.failedRequests) };

        std::ranges::transform(left.unitsByRecency, right.unitsByRecency, result.unitsByRecency.begin(), combine);
        return result;
    }
}
