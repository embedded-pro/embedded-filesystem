#ifndef FS_INTERFACES_RESULT_HPP
#define FS_INTERFACES_RESULT_HPP

#include "infra/stream/OutputStream.hpp"
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

    infra::TextOutputStream& operator<<(infra::TextOutputStream& stream, Result result);
    infra::TextOutputStream& operator<<(infra::TextOutputStream&& stream, Result result);
}

#endif
