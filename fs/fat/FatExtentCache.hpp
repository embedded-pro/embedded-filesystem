#ifndef FS_FAT_FAT_EXTENT_CACHE_HPP
#define FS_FAT_FAT_EXTENT_CACHE_HPP

#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatExtent
    {
        uint32_t index{ 0 };
        uint32_t cluster{ 0 };
        uint32_t length{ 0 };

        uint32_t End() const;
        bool Covers(uint32_t clusterIndex) const;
        uint32_t ClusterAt(uint32_t clusterIndex) const;

        bool operator==(const FatExtent& other) const = default;
    };

    class FatExtentCache
    {
    public:
        template<std::size_t Extents>
        class WithStorage;

        explicit FatExtentCache(infra::MemoryRange<FatExtent> storage);
        FatExtentCache(const FatExtentCache& other) = delete;
        FatExtentCache& operator=(const FatExtentCache& other) = delete;
        ~FatExtentCache();

        void Clear();
        void Add(const FatExtent& extent);
        void Truncate(uint32_t clusters);
        std::optional<FatExtent> Covering(uint32_t index);
        std::optional<FatExtent> NearestBefore(uint32_t index) const;
        std::optional<FatExtent> Last() const;

    private:
        bool Merge(const FatExtent& extent);
        void Replace(const FatExtent& extent);
        void Touch(std::size_t position);
        infra::MemoryRange<FatExtent> InUse();
        infra::MemoryRange<const FatExtent> InUse() const;

    private:
        infra::MemoryRange<FatExtent> storage;
        std::size_t count{ 0 };
    };

    namespace detail
    {
        template<std::size_t Extents>
        struct FatExtentStorage
        {
            std::array<FatExtent, Extents> extentStorage{};
        };
    }

    template<std::size_t Extents>
    class FatExtentCache::WithStorage
        : private detail::FatExtentStorage<Extents>
        , public FatExtentCache
    {
        static_assert(Extents > 0);

    public:
        WithStorage()
            : FatExtentCache(infra::MakeRange(this->extentStorage))
        {}
    };
}

#endif
