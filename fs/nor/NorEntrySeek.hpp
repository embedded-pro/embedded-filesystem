#ifndef FS_NOR_NOR_ENTRY_SEEK_HPP
#define FS_NOR_NOR_ENTRY_SEEK_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorEntrySeek
    {
    public:
        struct Entry
        {
            uint32_t key{ 0 };
            uint32_t inode{ 0 };
        };

        NorEntrySeek(NorNodeCache& cache, NorMapFetch& fetch);
        NorEntrySeek(const NorEntrySeek& other) = delete;
        NorEntrySeek& operator=(const NorEntrySeek& other) = delete;
        ~NorEntrySeek() = default;

        void Seek(uint32_t directory, const NorMapRoot& map, uint32_t key, const infra::Function<void(Result result)>& onDone);
        const std::optional<Entry>& Found() const;

    private:
        NorMapId Map() const;
        void FetchFrom(uint32_t from);
        void Fetched(NorFetchResult result);

    private:
        NorNodeCache& cache;
        NorMapFetch& fetch;
        uint32_t directory{ 0 };
        NorMapRoot map;
        uint32_t key{ 0 };
        std::optional<Entry> found;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
