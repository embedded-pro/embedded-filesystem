#ifndef FS_INTERFACES_DIRECTORY_HPP
#define FS_INTERFACES_DIRECTORY_HPP

#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include <optional>

namespace fs
{
    class Directory
    {
    protected:
        Directory() = default;
        Directory(const Directory& other) = delete;
        Directory& operator=(const Directory& other) = delete;
        ~Directory() = default;

    public:
        virtual void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) = 0;
    };
}

#endif
