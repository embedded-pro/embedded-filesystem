#include "fs/fat/FatClosedFiles.hpp"
#include <algorithm>

namespace fs
{
    FatClosedFiles::FatClosedFiles(infra::MemoryRange<FatClosedFile> storage)
        : storage(storage)
    {}

    FatClosedFiles::~FatClosedFiles() = default;

    void FatClosedFiles::Clear()
    {
        count = 0;
    }

    void FatClosedFiles::Remember(const FatClosedFile& file)
    {
        if (storage.empty())
            return;

        auto end = storage.begin() + count;
        auto found = std::find_if(storage.begin(), end, [&file](const FatClosedFile& remembered)
            {
                return remembered.entry == file.entry;
            });

        if (found == end && count != storage.size())
            ++count;
        else if (found == end)
            --found;

        std::move_backward(storage.begin(), found, found + 1);
        storage.front() = file;
    }

    std::optional<FatExtent> FatClosedFiles::Recall(FatEntryPosition entry, uint32_t firstCluster, uint32_t size) const
    {
        for (std::size_t index = 0; index != count; ++index)
            if (storage[index].entry == entry)
            {
                if (storage[index].firstCluster == firstCluster && storage[index].size == size)
                    return storage[index].last;

                return std::nullopt;
            }

        return std::nullopt;
    }
}
