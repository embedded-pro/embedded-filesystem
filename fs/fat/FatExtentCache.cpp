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
        auto extents = InUse();

        for (std::size_t position = 0; position != extents.size(); ++position)
            if (extents[position].Covers(index))
            {
                auto extent = extents[position];
                Touch(position);
                return extent;
            }

        return std::nullopt;
    }

    std::optional<FatExtent> FatExtentCache::NearestBefore(uint32_t index) const
    {
        std::optional<FatExtent> nearest;

        for (const auto& extent : InUse())
            if (extent.index <= index && (!nearest.has_value() || extent.index > nearest->index))
                nearest = extent;

        return nearest;
    }

    std::optional<FatExtent> FatExtentCache::Last() const
    {
        std::optional<FatExtent> last;

        for (const auto& extent : InUse())
            if (!last.has_value() || extent.End() > last->End())
                last = extent;

        return last;
    }

    bool FatExtentCache::Merge(const FatExtent& extent)
    {
        auto extents = InUse();

        for (std::size_t position = 0; position != extents.size(); ++position)
        {
            auto& cached = extents[position];

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
        auto extents = InUse();

        if (extents.size() == 1)
            return;

        std::rotate(extents.begin() + 1, extents.begin() + 2, extents.end());
        extents.back() = extent;
    }

    void FatExtentCache::Touch(std::size_t position)
    {
        auto extents = InUse();

        if (position != 0)
            std::rotate(extents.begin() + position, extents.begin() + position + 1, extents.end());
    }

    infra::MemoryRange<FatExtent> FatExtentCache::InUse()
    {
        return infra::Head(storage, count);
    }

    infra::MemoryRange<const FatExtent> FatExtentCache::InUse() const
    {
        return infra::Head(infra::MemoryRange<const FatExtent>(storage), count);
    }
}
