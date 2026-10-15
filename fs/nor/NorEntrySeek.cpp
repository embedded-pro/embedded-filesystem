#include "fs/nor/NorEntrySeek.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t entryValueBytes = 4;
    }

    NorEntrySeek::NorEntrySeek(NorNodeCache& cache, NorMapFetch& fetch)
        : cache(cache)
        , fetch(fetch)
    {}

    void NorEntrySeek::Seek(uint32_t directory, const NorMapRoot& map, uint32_t key, const infra::Function<void(Result result)>& onSought)
    {
        this->directory = directory;
        this->map = map;
        found = std::nullopt;
        onDone = onSought;
        FetchFrom(key);
    }

    const std::optional<NorEntrySeek::Entry>& NorEntrySeek::Found() const
    {
        return found;
    }

    NorMapId NorEntrySeek::Map() const
    {
        return NorMapId{ NorMap::directory, directory, entryValueBytes };
    }

    void NorEntrySeek::FetchFrom(uint32_t from)
    {
        key = from;
        cache.UnpinAll();
        fetch.Fetch(Map(), map, key, [this](NorFetchResult result)
            {
                Fetched(result);
            });
    }

    void NorEntrySeek::Fetched(NorFetchResult result)
    {
        // The cache fills with modified nodes only after a checkpoint failed for want of space
        if (result != NorFetchResult::fetched)
        {
            onDone(result == NorFetchResult::cacheFull ? Result::noSpace : Result::corrupted);
            return;
        }

        auto seek = NorBTree(cache, Map()).Seek(map, key);
        if (seek.nextLeaf.has_value())
            FetchFrom(*seek.nextLeaf);
        else
        {
            if (seek.entry.has_value())
                found = Entry{ seek.entry->key, LoadLittle32(seek.entry->value, 0) };

            onDone(Result::success);
        }
    }
}
