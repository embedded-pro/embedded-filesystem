#pragma once

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
