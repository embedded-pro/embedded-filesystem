#ifndef METRICS_WORKLOAD_PATTERN_HPP
#define METRICS_WORKLOAD_PATTERN_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>

namespace metrics
{
    void FillPattern(infra::ByteRange data, uint32_t fileId, uint32_t offset);
    bool MatchesPattern(infra::ConstByteRange data, uint32_t fileId, uint32_t offset);
}

#endif
