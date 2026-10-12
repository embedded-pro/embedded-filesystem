#ifndef FS_FAT_FAT_NAME_HPP
#define FS_FAT_FAT_NAME_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"

namespace fs
{
    Result ValidateFatName(infra::BoundedConstString name);
    char FoldAscii(char character);
    bool EqualIgnoringAsciiCase(infra::BoundedConstString x, infra::BoundedConstString y);
}

#endif
