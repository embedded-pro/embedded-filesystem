#ifndef FS_NOR_NOR_DIRECTORY_HPP
#define FS_NOR_NOR_DIRECTORY_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/Directory.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorDirectory
        : public Directory
        , public PooledHandle
    {
    public:
        explicit NorDirectory(NorVolume& volume);
        NorDirectory(const NorDirectory& other) = delete;
        NorDirectory& operator=(const NorDirectory& other) = delete;
        ~NorDirectory();

        void Open(uint32_t inode);
        void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) override;

    private:
        void UnReferenced() override;

        void Read();
        void Done(Result result);

    private:
        NorVolume& volume;
        OperationQueue::Claimer claimer;
        uint32_t inode{ 0 };
        uint32_t nextKey{ 0 };
        bool ended{ false };
        uint32_t generation{ 0 };
        infra::SharedPtr<NorDirectory> self;
        infra::AutoResetFunction<void(Result result, std::optional<EntryInfo> entry)> onDone;
    };
}

#endif
