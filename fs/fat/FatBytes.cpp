#include "fs/fat/FatBytes.hpp"

namespace fs
{
    namespace
    {
        constexpr std::size_t signatureOffset = 510;
        constexpr std::size_t sectorBytes = 512;
    }

    bool HasBootSignature(infra::ConstByteRange sector)
    {
        return sector.size() >= sectorBytes && LoadLittle16(sector, signatureOffset) == 0xaa55;
    }

    void StoreBootSignature(infra::ByteRange sector)
    {
        StoreLittle16(sector, signatureOffset, 0xaa55);
    }
}
