#pragma once

#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"

namespace fs
{
    Result ValidateFatName(infra::BoundedConstString name);
    char FoldAscii(char character);
    bool EqualIgnoringAsciiCase(infra::BoundedConstString x, infra::BoundedConstString y);
}
