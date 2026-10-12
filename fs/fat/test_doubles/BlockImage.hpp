#ifndef FS_FAT_TEST_DOUBLES_BLOCK_IMAGE_HPP
#define FS_FAT_TEST_DOUBLES_BLOCK_IMAGE_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>

namespace fs
{
    class BlockImage
    {
    protected:
        BlockImage() = default;
        BlockImage(const BlockImage& other) = delete;
        BlockImage& operator=(const BlockImage& other) = delete;
        ~BlockImage() = default;

    public:
        virtual uint32_t Blocks() const = 0;
        virtual void ReadBlock(uint32_t block, infra::ByteRange out) const = 0;
        virtual void WriteBlock(uint32_t block, infra::ConstByteRange data) = 0;
    };
}

#endif
