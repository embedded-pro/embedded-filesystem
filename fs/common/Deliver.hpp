#ifndef FS_COMMON_DELIVER_HPP
#define FS_COMMON_DELIVER_HPP

#include "fs/interfaces/Directory.hpp"
#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <optional>

namespace fs::detail
{
    inline void Deliver(const infra::Function<void(Result result)>& onDone, Result result)
    {
        onDone(result);
    }

    inline void Deliver(const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone, Result result)
    {
        onDone(result, nullptr);
    }

    inline void Deliver(const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone, Result result)
    {
        onDone(result, nullptr);
    }

    inline void Deliver(const infra::Function<void(Result result, const EntryInfo& info)>& onDone, Result result)
    {
        onDone(result, EntryInfo{});
    }

    inline void Deliver(const infra::Function<void(Result result, VolumeUsage usage)>& onDone, Result result)
    {
        onDone(result, VolumeUsage{});
    }

    inline void Deliver(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone, Result result)
    {
        onDone(result, std::nullopt);
    }

    inline void Deliver(const infra::Function<void(Result result, std::size_t bytesRead)>& onDone, Result result)
    {
        onDone(result, 0);
    }
}

#endif
