#ifndef FS_INTERFACES_TEST_DOUBLES_FILE_SYSTEM_MOCK_HPP
#define FS_INTERFACES_TEST_DOUBLES_FILE_SYSTEM_MOCK_HPP

#include "fs/interfaces/FileSystem.hpp"
#include "gmock/gmock.h"

namespace fs
{
    class FileSystemMock
        : public FileSystem
    {
    public:
        MOCK_METHOD(void, Mount, (const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Unmount, (const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Open, (infra::BoundedConstString path, OpenMode mode, (const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)), (override));
        MOCK_METHOD(void, OpenDirectory, (infra::BoundedConstString path, (const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)), (override));
        MOCK_METHOD(void, Stat, (infra::BoundedConstString path, (const infra::Function<void(Result result, const EntryInfo& info)>& onDone)), (override));
        MOCK_METHOD(void, Remove, (infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Rename, (infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, MakeDirectory, (infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Usage, ((const infra::Function<void(Result result, VolumeUsage usage)>& onDone)), (override));
        MOCK_METHOD(bool, Idle, (), (const, override));
    };
}

#endif
