#ifndef METRICS_BASELINES_FATFS_FAT_FS_SUBJECT_HPP
#define METRICS_BASELINES_FATFS_FAT_FS_SUBJECT_HPP

#include "ff.h"
#include "metrics/baselines/SynchronousDevices.hpp"
#include "metrics/workload/CompletionQueue.hpp"
#include "metrics/workload/FileSystemUnderTest.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace metrics
{
    fs::Result FatFsResult(FRESULT result);

    class FatFsSubject
        : public FileSystemUnderTest
    {
    public:
        static constexpr std::size_t maxOpenFiles = 2;
        static constexpr std::size_t linkMapEntries = 64;
        static constexpr bool tuned = FF_USE_FASTSEEK != 0;

        FatFsSubject(VirtualTime& time, hal::BlockDevice& device, uint32_t allocationUnitBlocks);
        ~FatFsSubject();

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

    private:
        struct OpenFile
        {
            FIL file{};
            std::array<DWORD, linkMapEntries> linkMap{};
            bool open{ false };
        };

        void Complete(const std::function<void(fs::Result result)>& onDone, FRESULT result);
        FRESULT Open(OpenFile& slot, std::string_view path, OpenMode mode);
        void CreateLinkMap(OpenFile& slot);
        FRESULT List(std::string_view path, std::vector<DirectoryEntry>& entries);
        FIL& File(FileHandle file) const;

    private:
        SynchronousBlockDevice device;
        uint32_t allocationUnitBlocks;
        FATFS volume{};
        mutable std::array<OpenFile, maxOpenFiles> files;
        CompletionQueue completions;
    };
}

#endif
