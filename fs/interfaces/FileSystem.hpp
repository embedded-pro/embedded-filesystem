#ifndef FS_INTERFACES_FILE_SYSTEM_HPP
#define FS_INTERFACES_FILE_SYSTEM_HPP

#include "fs/interfaces/Directory.hpp"
#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/interfaces/OpenMode.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>

namespace fs
{
    struct VolumeUsage
    {
        uint64_t usedBytes{ 0 };
        uint64_t totalBytes{ 0 };

        bool operator==(const VolumeUsage& other) const = default;
    };

    class FileSystem
    {
    protected:
        FileSystem() = default;
        FileSystem(const FileSystem& other) = delete;
        FileSystem& operator=(const FileSystem& other) = delete;
        ~FileSystem() = default;

    public:
        virtual void Mount(const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Unmount(const infra::Function<void(Result result)>& onDone) = 0;

        virtual void Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone) = 0;
        virtual void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone) = 0;
        virtual void Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone) = 0;
        virtual void Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone) = 0;

        // No call queued or running and no background work scheduled: the volume does nothing until it is called again
        virtual bool Idle() const = 0;
    };
}

#endif
