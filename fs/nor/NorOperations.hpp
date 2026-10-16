#ifndef FS_NOR_NOR_OPERATIONS_HPP
#define FS_NOR_NOR_OPERATIONS_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class NorVolume;
    struct NorOpenFile;

    template<class Call>
    using NorPathOperation = void (*)(NorVolume& volume, Call&& call, const infra::Function<void(Result result)>& onFinished);
    using NorTruncateOperation = void (*)(NorVolume& volume, NorOpenFile& file, uint32_t size, const infra::Function<void(Result result)>& onDone);
    using NorFormatOperation = void (*)(NorVolume& volume, const infra::Function<void(Result result)>& onDone);
    using NorOverwriteOperation = void (*)(NorVolume& volume, NorOpenFile& file, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone);

    struct NorOperations
    {
        NorPathOperation<NorMakeDirectoryCall> makeDirectory{ nullptr };
        NorPathOperation<NorRemoveCall> remove{ nullptr };
        NorPathOperation<NorRenameCall> rename{ nullptr };
        NorTruncateOperation truncate{ nullptr };
        NorFormatOperation format{ nullptr };
        NorOverwriteOperation overwrite{ nullptr };
    };
}

#endif
