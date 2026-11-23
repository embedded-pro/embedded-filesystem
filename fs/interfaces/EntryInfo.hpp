#pragma once

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
