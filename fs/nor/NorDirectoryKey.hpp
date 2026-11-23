#pragma once

#include "infra/util/BoundedString.hpp"
#include <cstdint>

namespace fs
{
    uint32_t NorDirectoryKey(infra::BoundedConstString name);
    uint32_t NorLastDirectoryKey(uint32_t key);
}
