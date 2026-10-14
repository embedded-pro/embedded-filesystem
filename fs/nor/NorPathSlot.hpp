#ifndef FS_NOR_NOR_PATH_SLOT_HPP
#define FS_NOR_NOR_PATH_SLOT_HPP

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
    struct NorStatCall
    {
        infra::BoundedConstString path;
        infra::AutoResetFunction<void(Result result, const EntryInfo& info)> onDone;
    };

    struct NorOpenDirectoryCall
    {
        infra::BoundedConstString path;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<Directory> directory)> onDone;
    };

    struct NorOpenCall
    {
        infra::BoundedConstString path;
        OpenMode mode{ OpenMode::read };
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<File> file)> onDone;
    };

    struct NorMakeDirectoryCall
    {
        infra::BoundedConstString path;
        infra::AutoResetFunction<void(Result result)> onDone;
    };

    using NorPathCall = std::variant<NorStatCall, NorOpenDirectoryCall, NorOpenCall, NorMakeDirectoryCall>;

    struct NorPathSlot
    {
        explicit NorPathSlot(OperationQueue& queue);
        NorPathSlot(const NorPathSlot& other) = delete;
        NorPathSlot& operator=(const NorPathSlot& other) = delete;
        ~NorPathSlot();

        bool InUse() const;

        OperationQueue::Claimer claimer;
        std::optional<NorPathCall> call;
    };
}

#endif
