#include "fs/nor/NorOpenFile.hpp"
#include <algorithm>

namespace fs
{
    bool NorOpenFile::Writable() const
    {
        return mode != OpenMode::read;
    }

    bool NorOpenFile::Uncommitted() const
    {
        return size != committedSize || !entries.empty();
    }

    infra::ByteRange NorOpenFile::Pending() const
    {
        return infra::Head(buffer, size - programmed);
    }

    std::optional<uint32_t> NorOpenFile::UncommittedBlock(uint32_t index) const
    {
        auto entry = std::ranges::find(entries, index, &NorBlockEntry::index);
        if (entry == entries.end())
            return std::nullopt;

        return entry->block;
    }
}
