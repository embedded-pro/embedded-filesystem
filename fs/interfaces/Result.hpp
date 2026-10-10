#ifndef FS_INTERFACES_RESULT_HPP
#define FS_INTERFACES_RESULT_HPP

#include <cstdint>

namespace fs
{
    enum class Result : uint8_t
    {
        success,
        notFound,
        alreadyExists,
        notADirectory,
        isADirectory,
        notEmpty,
        invalidPath,
        nameTooLong,
        noSpace,
        tooManyOpenFiles,
        busy,
        readOnly,
        notMounted,
        unsupported,
        corrupted,
        mediaRemoved,
        deviceError
    };
}

#endif
