#include "fs/nor/NorNodeBlocks.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t bitsPerByte = 8;

        uint8_t MaskOf(uint32_t block)
        {
            return static_cast<uint8_t>(1u << (block % bitsPerByte));
        }
    }

    NorNodeBlocks::NorNodeBlocks(infra::ByteRange storage)
        : storage(storage)
    {}

    void NorNodeBlocks::Reset(uint32_t blockCount)
    {
        really_assert(NorNodeBlockBytes(blockCount) <= storage.size());

        blocks = blockCount;
        count = 0;
        std::ranges::fill(infra::Head(storage, NorNodeBlockBytes(blockCount)), uint8_t{ 0 });
    }

    bool NorNodeBlocks::Contains(uint32_t block) const
    {
        really_assert(block < blocks);

        return (storage[block / bitsPerByte] & MaskOf(block)) != 0;
    }

    void NorNodeBlocks::Add(uint32_t block)
    {
        if (!Contains(block))
        {
            storage[block / bitsPerByte] |= MaskOf(block);
            ++count;
        }
    }

    void NorNodeBlocks::Remove(uint32_t block)
    {
        if (Contains(block))
        {
            storage[block / bitsPerByte] &= static_cast<uint8_t>(~MaskOf(block));
            --count;
        }
    }

    uint32_t NorNodeBlocks::Count() const
    {
        return count;
    }
}
