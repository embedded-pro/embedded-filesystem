#ifndef METRICS_WORKLOAD_TEST_DOUBLES_FAKE_FILE_SYSTEM_HPP
#define METRICS_WORKLOAD_TEST_DOUBLES_FAKE_FILE_SYSTEM_HPP

#include "hal/interfaces/Flash.hpp"
#include "metrics/workload/CompletionQueue.hpp"
#include "metrics/workload/FileSystemUnderTest.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace metrics
{
    class FakeFileSystem
        : public FileSystemUnderTest
    {
    public:
        explicit FakeFileSystem(hal::Flash& flash);

        std::string_view Name() const override;
        std::size_t RamBytes() const override;

        void Format(const std::function<void(fs::Result result)>& onDone) override;
        void Mount(const std::function<void(fs::Result result)>& onDone) override;
        void Unmount(const std::function<void(fs::Result result)>& onDone) override;

        void Open(std::string_view path, OpenMode mode, const std::function<void(fs::Result result, FileHandle file)>& onDone) override;
        void Read(FileHandle file, infra::ByteRange buffer, const std::function<void(fs::Result result, std::size_t bytesRead)>& onDone) override;
        void Write(FileHandle file, infra::ConstByteRange data, const std::function<void(fs::Result result)>& onDone) override;
        void Seek(FileHandle file, uint32_t position) override;
        uint32_t Size(FileHandle file) const override;
        void Sync(FileHandle file, const std::function<void(fs::Result result)>& onDone) override;
        void Close(FileHandle file, const std::function<void(fs::Result result)>& onDone) override;
        void ExpectSize(FileHandle file, uint32_t bytes, const std::function<void(fs::Result result)>& onDone) override;

        void Remove(std::string_view path, const std::function<void(fs::Result result)>& onDone) override;
        void MakeDirectory(std::string_view path, const std::function<void(fs::Result result)>& onDone) override;
        void List(std::string_view path, const std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)>& onDone) override;

        void IdleHint(const std::function<void()>& onDone) override;

        bool completeSynchronously{ false };
        bool neverComplete{ false };
        uint32_t idleHints{ 0 };
        std::map<std::string, std::vector<uint8_t>> files;
        std::set<std::string> directories;

    private:
        struct OpenFile
        {
            std::string path;
            uint32_t position;
        };

        void Complete(const std::function<void(fs::Result result)>& onDone, fs::Result result);
        uint32_t DeviceAddress(FileHandle file, std::size_t size) const;
        uint64_t UsedBytes() const;

    private:
        hal::Flash& flash;
        std::vector<OpenFile> openFiles;
        std::vector<uint8_t> deviceBuffer;
        std::function<void(fs::Result result)> pendingCompletion;
        CompletionQueue completions;
    };
}

#endif
