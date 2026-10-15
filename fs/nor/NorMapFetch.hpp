#ifndef FS_NOR_NOR_MAP_FETCH_HPP
#define FS_NOR_NOR_MAP_FETCH_HPP

#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    enum class NorFetchResult : uint8_t
    {
        fetched,
        cacheFull,
        corrupted
    };

    class NorMapFetch
    {
    public:
        explicit NorMapFetch(NorNodeCache& cache);
        NorMapFetch(const NorMapFetch& other) = delete;
        NorMapFetch& operator=(const NorMapFetch& other) = delete;
        ~NorMapFetch();

        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, const infra::Function<void(NorFetchResult result)>& onDone);
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, uint8_t lowestLevel, const infra::Function<void(NorFetchResult result)>& onDone);

    private:
        void Descend();
        void Loaded(std::optional<std::size_t> slot);
        bool Enter(std::size_t slot, bool fromFlash);

    private:
        NorNodeCache& cache;
        NorMapId map;
        uint32_t key{ 0 };
        uint32_t address{ 0 };
        uint8_t level{ 0 };
        uint8_t lowestLevel{ 0 };
        uint32_t firstIndex{ 0 };
        infra::AutoResetFunction<void(NorFetchResult result)> onDone;
    };
}

#endif
