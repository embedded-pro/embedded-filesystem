#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"

namespace fs
{
    NorMapFetch::NorMapFetch(NorNodeCache& cache)
        : cache(cache)
    {}

    NorMapFetch::~NorMapFetch() = default;

    void NorMapFetch::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, const infra::Function<void(NorFetchResult result)>& onFetched)
    {
        Fetch(map, root, key, 0, onFetched);
    }

    void NorMapFetch::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, uint8_t lowest, const infra::Function<void(NorFetchResult result)>& onFetched)
    {
        this->map = map;
        this->key = key;
        address = root.address;
        level = static_cast<uint8_t>(root.height - 1);
        lowestLevel = lowest;
        firstIndex = 0;
        onDone = onFetched;

        if ((root.address == 0) != (root.height == 0) || root.height > norMaxLevels)
        {
            onDone(NorFetchResult::corrupted);
            return;
        }

        if (map.map == NorMap::fileData && !NorRadixTree::Covers(root, key, cache.NodeSize()))
            address = 0;

        Descend();
    }

    void NorMapFetch::Descend()
    {
        while (address != 0)
        {
            auto slot = cache.Find(address);
            if (!slot.has_value())
            {
                if (!cache.HasRoom())
                    onDone(NorFetchResult::cacheFull);
                else
                    cache.Load(address, [this](std::optional<std::size_t> loaded)
                        {
                            Loaded(loaded);
                        });

                return;
            }

            if (!Enter(slot.value_or(0), false))
            {
                onDone(NorFetchResult::corrupted);
                return;
            }
        }

        onDone(NorFetchResult::fetched);
    }

    void NorMapFetch::Loaded(std::optional<std::size_t> slot)
    {
        if (slot.has_value() && Enter(slot.value_or(0), true))
            Descend();
        else
            onDone(NorFetchResult::corrupted);
    }

    bool NorMapFetch::Enter(std::size_t slot, bool fromFlash)
    {
        auto node = cache.Node(slot);
        auto header = ReadNorNodeHeader(node);
        if (header.map != map.map || header.owner != map.owner || header.level != level)
            return false;

        if (map.map == NorMap::fileData)
        {
            if (header.lowestKey != firstIndex || (fromFlash && !NorRadixTree::HasValidCount(node)))
                return false;
        }
        else if (fromFlash && !NorBTree::HasValidCount(node, map.valueBytes))
            return false;

        cache.Pin(slot);

        if (level == lowestLevel)
        {
            address = 0;
            return true;
        }

        if (map.map == NorMap::fileData)
        {
            address = NorRadixTree::ChildFor(node, key);
            firstIndex = NorRadixTree::FirstIndexOfChild(node, key);
        }
        else
            address = NorBTree::ChildFor(node, key);

        --level;
        return true;
    }
}
