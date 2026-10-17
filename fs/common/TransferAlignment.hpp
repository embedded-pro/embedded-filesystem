#ifndef FS_COMMON_TRANSFER_ALIGNMENT_HPP
#define FS_COMMON_TRANSFER_ALIGNMENT_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>

namespace fs
{
    // The cache line of a Cortex-M7: a driver that moves data with DMA while the data cache is on cleans and invalidates
    // whole lines, so the buffers of fs that a driver transfers are aligned to it
    constexpr std::size_t bufferAlignment = 32;

    class TransferAlignment
    {
    public:
        explicit TransferAlignment(std::size_t alignment);

        bool Accepts(infra::ConstByteRange buffer) const;

    private:
        std::size_t alignment;
    };
}

#endif
