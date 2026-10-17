#ifndef FS_FAT_READER_FAT_READER_FEATURES_HPP
#define FS_FAT_READER_FAT_READER_FEATURES_HPP

#include "fs/fat/FatEntryAssembler.hpp"

namespace fs
{
    struct FatReaderFeatures
    {
        bool longNames{ true };
    };

    template<FatReaderFeatures Features>
    constexpr FatEntryAssembler::Reading NameReadingFor()
    {
        if constexpr (Features.longNames)
            return &FatEntryAssembler::ReadLongNames;
        else
            return &FatEntryAssembler::ReadShortNames;
    }
}

#endif
