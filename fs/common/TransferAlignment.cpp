#include "fs/common/TransferAlignment.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <bit>
#include <cstdint>

namespace fs
{
    TransferAlignment::TransferAlignment(std::size_t alignment)
        : alignment(alignment)
    {
        really_assert(std::has_single_bit(alignment) && alignment <= bufferAlignment);
    }

    bool TransferAlignment::Accepts(infra::ConstByteRange buffer) const
    {
        return std::bit_cast<std::uintptr_t>(buffer.begin()) % alignment == 0;
    }
}
