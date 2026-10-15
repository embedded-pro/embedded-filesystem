#include "fs/nor/NorInodeRead.hpp"
#include "fs/nor/NorBTree.hpp"
#include <optional>

namespace fs
{
    NorInodeRead::NorInodeRead(NorNodeCache& cache, NorMapFetch& fetch, const NorConfig& config)
        : cache(cache)
        , fetch(fetch)
        , config(config)
    {}

    void NorInodeRead::Read(const NorMapRoot& inodeTable, uint32_t inode, const infra::Function<void(Result result)>& onRead)
    {
        this->inodeTable = inodeTable;
        this->inode = inode;
        onDone = onRead;
        cache.UnpinAll();
        fetch.Fetch(NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(config) }, inodeTable, inode, [this](NorFetchResult result)
            {
                Fetched(result);
            });
    }

    const NorInodeRecord& NorInodeRead::Record() const
    {
        return record;
    }

    void NorInodeRead::Fetched(NorFetchResult result)
    {
        // The cache fills with modified nodes only after a checkpoint failed for want of space
        if (result == NorFetchResult::cacheFull)
        {
            onDone(Result::noSpace);
            return;
        }

        std::optional<NorInodeRecord> parsed;
        if (result == NorFetchResult::fetched)
        {
            auto bytes = NorBTree(cache, NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(config) }).Find(inodeTable, inode);
            if (bytes.has_value())
                parsed = ParseNorInodeRecord(*bytes, config);
        }

        if (!parsed.has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        record = *parsed;
        onDone(Result::success);
    }
}
