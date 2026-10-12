#ifndef FS_FAT_FAT_PATH_SLOT_HPP
#define FS_FAT_FAT_PATH_SLOT_HPP

#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/Directory.hpp"
#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/interfaces/OpenMode.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/SharedPtr.hpp"
#include <optional>
#include <variant>

namespace fs
{
    struct FatStatCall
    {
        infra::BoundedConstString path;
        infra::AutoResetFunction<void(Result result, const EntryInfo& info)> onDone;
    };

    struct FatOpenDirectoryCall
    {
        infra::BoundedConstString path;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<Directory> directory)> onDone;
    };

    struct FatOpenCall
    {
        infra::BoundedConstString path;
        OpenMode mode{ OpenMode::read };
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<File> file)> onDone;
    };

    using FatPathCall = std::variant<FatStatCall, FatOpenDirectoryCall, FatOpenCall>;

    struct FatPathSlot
    {
        explicit FatPathSlot(OperationQueue& queue);
        FatPathSlot(const FatPathSlot& other) = delete;
        FatPathSlot& operator=(const FatPathSlot& other) = delete;
        ~FatPathSlot();

        bool InUse() const;

        OperationQueue::Claimer claimer;
        std::optional<FatPathCall> call;
    };
}

#endif
