#ifndef FS_FAT_FAT_PATH_SLOT_HPP
#define FS_FAT_FAT_PATH_SLOT_HPP

#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include <variant>

namespace fs
{
    struct FatStatCall
    {
        infra::BoundedConstString path;
        infra::AutoResetFunction<void(Result result, const EntryInfo& info)> onDone;
    };

    using FatPathCall = std::variant<std::monostate, FatStatCall>;

    struct FatPathSlot
    {
        explicit FatPathSlot(OperationQueue& queue);
        FatPathSlot(const FatPathSlot& other) = delete;
        FatPathSlot& operator=(const FatPathSlot& other) = delete;
        ~FatPathSlot();

        bool InUse() const;

        OperationQueue::Claimer claimer;
        FatPathCall call;
    };
}

#endif
