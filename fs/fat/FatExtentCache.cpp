#include "fs/fat/FatExtentCache.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        bool Contiguous(const FatExtent& first, const FatExtent& second)
        {
            return first.index <= second.index && second.index <= first.End() && first.ClusterAt(second.index) == second.cluster;
        }
    }

    uint32_t FatExtent::End() const
    {
        return index + length;
    }

    bool FatExtent::Covers(uint32_t clusterIndex) const
    {
        return index <= clusterIndex && clusterIndex < End();
    }

    uint32_t FatExtent::ClusterAt(uint32_t clusterIndex) const
    {
        return cluster + (clusterIndex - index);
    }

    FatExtentCache::FatExtentCache(infra::MemoryRange<FatExtent> storage)
        : storage(storage)
    {}

    FatExtentCache::~FatExtentCache() = default;

    void FatExtentCache::Clear()
    {
        count = 0;
    }

    void FatExtentCache::Add(const FatExtent& extent)
    {
        if (Merge(extent))
            return;

        if (count != storage.size())
            storage[count++] = extent;
        else
            Replace(extent);
    }

    std::optional<FatExtent> FatExtentCache::Covering(uint32_t index)
    {
        for (std::size_t position = 0; position != count; ++position)
            if (storage[position].Covers(index))
            {
                auto extent = storage[position];
                Touch(position);
                return extent;
            }

        return std::nullopt;
    }

    std::optional<FatExtent> FatExtentCache::NearestBefore(uint32_t index) const
    {
        std::optional<FatExtent> nearest;

        for (std::size_t position = 0; position != count; ++position)
            if (storage[position].index <= index && (!nearest || storage[position].index > nearest->index))
                nearest = storage[position];

        return nearest;
    }

    std::optional<FatExtent> FatExtentCache::Last() const
    {
        std::optional<FatExtent> last;

        for (std::size_t position = 0; position != count; ++position)
            if (!last || storage[position].End() > last->End())
                last = storage[position];

        return last;
    }

    bool FatExtentCache::Merge(const FatExtent& extent)
    {
        for (std::size_t position = 0; position != count; ++position)
        {
            auto& cached = storage[position];

            if (Contiguous(cached, extent) || Contiguous(extent, cached))
            {
                auto begin = std::min(cached.index, extent.index);
                auto end = std::max(cached.End(), extent.End());
                cached = FatExtent{ begin, begin == cached.index ? cached.cluster : extent.cluster, end - begin };
                Touch(position);
                return true;
            }
        }

        return false;
    }

    void FatExtentCache::Replace(const FatExtent& extent)
    {
        if (count == 1)
            return;

        std::rotate(storage.begin() + 1, storage.begin() + 2, storage.begin() + count);
        storage[count - 1] = extent;
    }

    void FatExtentCache::Touch(std::size_t position)
    {
        if (position != 0)
            std::rotate(storage.begin() + position, storage.begin() + position + 1, storage.begin() + count);
    }
}
