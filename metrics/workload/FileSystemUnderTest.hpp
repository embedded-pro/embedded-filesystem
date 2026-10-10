#ifndef METRICS_WORKLOAD_FILE_SYSTEM_UNDER_TEST_HPP
#define METRICS_WORKLOAD_FILE_SYSTEM_UNDER_TEST_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace metrics
{
    enum class OpenMode : uint8_t
    {
        read,
        write,
        append,
        readWrite
    };

    using FileHandle = uint32_t;

    struct VolumeUsage
    {
        uint64_t usedBytes;
        uint64_t totalBytes;

        bool operator==(const VolumeUsage& other) const = default;
    };

    struct DirectoryEntry
    {
        std::string name;
        bool directory;
        uint32_t size;

        bool operator==(const DirectoryEntry& other) const = default;
    };

    class FileSystemUnderTest
    {
    protected:
        FileSystemUnderTest() = default;
        FileSystemUnderTest(const FileSystemUnderTest& other) = delete;
        FileSystemUnderTest& operator=(const FileSystemUnderTest& other) = delete;
        ~FileSystemUnderTest() = default;

    public:
        virtual std::string_view Name() const = 0;
        virtual std::size_t RamBytes() const = 0;

        virtual void Format(const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void Mount(const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void Unmount(const std::function<void(fs::Result result)>& onDone) = 0;

        virtual void Open(std::string_view path, OpenMode mode, const std::function<void(fs::Result result, FileHandle file)>& onDone) = 0;
        virtual void Read(FileHandle file, infra::ByteRange buffer, const std::function<void(fs::Result result, std::size_t bytesRead)>& onDone) = 0;
        virtual void Write(FileHandle file, infra::ConstByteRange data, const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void Seek(FileHandle file, uint32_t position) = 0;
        virtual uint32_t Size(FileHandle file) const = 0;
        virtual void Sync(FileHandle file, const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void Close(FileHandle file, const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void ExpectSize(FileHandle file, uint32_t bytes, const std::function<void(fs::Result result)>& onDone) = 0;

        virtual void Remove(std::string_view path, const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void MakeDirectory(std::string_view path, const std::function<void(fs::Result result)>& onDone) = 0;
        virtual void List(std::string_view path, const std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)>& onDone) = 0;

        virtual void QueryUsage(const std::function<void(fs::Result result, VolumeUsage usage)>& onDone) = 0;
        virtual void IdleHint(const std::function<void()>& onDone) = 0;
    };
}

#endif
