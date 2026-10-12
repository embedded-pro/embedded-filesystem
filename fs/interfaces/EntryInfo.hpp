#ifndef FS_INTERFACES_ENTRY_INFO_HPP
#define FS_INTERFACES_ENTRY_INFO_HPP

#include "infra/util/BoundedString.hpp"
#include <cstdint>

namespace fs
{
    enum class EntryType : uint8_t
    {
        file,
        directory
    };

    struct EntryInfo
    {
        infra::BoundedConstString name;
        EntryType type{ EntryType::file };
        uint32_t size{ 0 };

        bool operator==(const EntryInfo& other) const = default;
    };
}

#endif
