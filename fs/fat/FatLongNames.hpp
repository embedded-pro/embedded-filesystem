#ifndef FS_FAT_FAT_LONG_NAMES_HPP
#define FS_FAT_FAT_LONG_NAMES_HPP

#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatLongNames
    {
        uint8_t (*entriesFor)(infra::BoundedConstString name);
        void (*encode)(infra::BoundedConstString name, uint8_t ordinal, bool last, uint8_t checksum, infra::ByteRange out);
        ShortNameBytes (*basis)(infra::BoundedConstString name);
        ShortNameBytes (*withTail)(const ShortNameBytes& basis, uint32_t tail);
        std::optional<uint32_t> (*tailOf)(const ShortNameBytes& name, const ShortNameBytes& basis);
        ShortNameBytes (*hashed)(const ShortNameBytes& basis, infra::BoundedConstString name);
    };

    inline constexpr FatLongNames fatLongNames{ &LongEntriesFor, &EncodeLongEntry, &BasisName, &WithNumericTail, &NumericTailOf, &HashedAlias };
}

#endif
