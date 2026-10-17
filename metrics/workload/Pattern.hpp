#ifndef METRICS_WORKLOAD_PATTERN_HPP
#define METRICS_WORKLOAD_PATTERN_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>

// Header-only, so that the workloads on the boards (docs/Validation.md) write the same data as the host
namespace metrics
{
    namespace detail
    {
        inline uint8_t PatternByte(uint32_t fileId, uint32_t offset)
        {
            uint32_t value = ((fileId + 1) * 0x9e3779b1u) ^ (offset * 0x85ebca6bu);
            return static_cast<uint8_t>(value ^ (value >> 13) ^ (value >> 24));
        }
    }

    inline void FillPattern(infra::ByteRange data, uint32_t fileId, uint32_t offset)
    {
        for (std::size_t index = 0; index != data.size(); ++index)
            data[index] = detail::PatternByte(fileId, offset + static_cast<uint32_t>(index));
    }

    inline bool MatchesPattern(infra::ConstByteRange data, uint32_t fileId, uint32_t offset)
    {
        for (std::size_t index = 0; index != data.size(); ++index)
            if (data[index] != detail::PatternByte(fileId, offset + static_cast<uint32_t>(index)))
                return false;

        return true;
    }
}

#endif
