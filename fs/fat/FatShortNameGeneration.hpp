#ifndef FS_FAT_FAT_SHORT_NAME_GENERATION_HPP
#define FS_FAT_FAT_SHORT_NAME_GENERATION_HPP

#include "fs/fat/FatShortName.hpp"
#include "infra/util/BoundedString.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace fs
{
    using ShortNameBytes = std::array<uint8_t, 11>;

    std::optional<ShortName> ExactShortName(infra::BoundedConstString name);
    std::optional<ShortNameBytes> ShortFormOf(infra::BoundedConstString name);
    ShortNameBytes BasisName(infra::BoundedConstString name);
    ShortNameBytes WithNumericTail(const ShortNameBytes& basis, uint32_t tail);
    std::optional<uint32_t> NumericTailOf(const ShortNameBytes& name, const ShortNameBytes& basis);
    ShortNameBytes HashedAlias(const ShortNameBytes& basis, infra::BoundedConstString name);
}

#endif
