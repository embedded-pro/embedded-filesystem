#ifndef FS_NOR_NOR_DIRECTORY_KEY_HPP
#define FS_NOR_NOR_DIRECTORY_KEY_HPP

#include "infra/util/BoundedString.hpp"
#include <cstdint>

namespace fs
{
    uint32_t NorDirectoryKey(infra::BoundedConstString name);
    uint32_t NorLastDirectoryKey(uint32_t key);
}

#endif
