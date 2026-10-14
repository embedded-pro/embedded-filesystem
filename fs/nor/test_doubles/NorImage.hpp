#ifndef FS_NOR_TEST_DOUBLES_NOR_IMAGE_HPP
#define FS_NOR_TEST_DOUBLES_NOR_IMAGE_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>

namespace fs
{
    class NorImage
    {
    protected:
        NorImage() = default;
        NorImage(const NorImage& other) = delete;
        NorImage& operator=(const NorImage& other) = delete;
        ~NorImage() = default;

    public:
        virtual uint32_t Sectors() const = 0;
        virtual uint32_t SectorSize() const = 0;
        virtual void Read(uint32_t address, infra::ByteRange out) const = 0;
        virtual void Program(uint32_t address, infra::ConstByteRange data) = 0;
        virtual void EraseSector(uint32_t sector) = 0;
    };
}

#endif
