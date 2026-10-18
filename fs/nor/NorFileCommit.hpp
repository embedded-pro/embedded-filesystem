#ifndef FS_NOR_NOR_FILE_COMMIT_HPP
#define FS_NOR_NOR_FILE_COMMIT_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorFileCommit
    {
    public:
        explicit NorFileCommit(NorVolume& volume);
        NorFileCommit(const NorFileCommit& other) = delete;
        NorFileCommit& operator=(const NorFileCommit& other) = delete;
        ~NorFileCommit();

        void Commit(NorOpenFile& file, NorReserve reserve, const infra::Function<void(Result result)>& onDone);

    private:
        enum class Step : uint8_t
        {
            retry,
            recordFetched,
            firstLeafFetched,
            secondLeafFetched,
            committed,
            finish
        };

        void Retry(Result result);
        void FetchPaths();
        void RecordFetched();
        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, Step next);
        void Fetched();
        void CheckRoom();
        void CheckpointAndRetry();
        void Write();
        void Committed(Result result);
        void Finish(Result result);
        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);
        NorMapId InodeTable() const;
        NorMapId DataMap() const;

    private:
        NorVolume& volume;
        NorOpenFile* file{ nullptr };
        NorReserve reserve{ NorReserve::used };
        Step step{ Step::retry };
        NorMapRoot dataMap;
        std::optional<uint32_t> secondLeafIndex;
        infra::Function<void(Result result)> resume;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
