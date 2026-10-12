#ifndef METRICS_BASELINES_LITTLEFS_LITTLE_FS_SUBJECT_HPP
#define METRICS_BASELINES_LITTLEFS_LITTLE_FS_SUBJECT_HPP

#include "lfs.h"
#include "metrics/baselines/SynchronousDevices.hpp"
#include "metrics/workload/CompletionQueue.hpp"
#include "metrics/workload/FileSystemUnderTest.hpp"
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace metrics
{
    struct LittleFsConfiguration
    {
        std::string_view name;
        lfs_size_t readSize;
        lfs_size_t programSize;
        lfs_size_t cacheSize;
        lfs_size_t lookaheadSize;
        int32_t blockCycles;
        bool collectGarbageWhenIdle;
    };

    extern const LittleFsConfiguration littleFsDefault;
    extern const LittleFsConfiguration littleFsRamMatched;

    fs::Result LittleFsResult(int error);

    class LittleFsSubject
        : public FileSystemUnderTest
    {
    public:
        static constexpr std::size_t maxOpenFiles = 2;

        LittleFsSubject(VirtualTime& time, hal::Flash& flash, const LittleFsConfiguration& configuration);

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

        void QueryUsage(const std::function<void(fs::Result result, VolumeUsage usage)>& onDone) override;
        void IdleHint(const std::function<void()>& onDone) override;

    private:
        struct OpenFile
        {
            lfs_file_t file{};
            lfs_file_config config{};
            std::vector<uint8_t> cache;
            bool open{ false };
        };

        static LittleFsSubject& Of(const lfs_config* config);
        static int ReadBlock(const lfs_config* config, lfs_block_t block, lfs_off_t offset, void* buffer, lfs_size_t size);
        static int ProgramBlock(const lfs_config* config, lfs_block_t block, lfs_off_t offset, const void* buffer, lfs_size_t size);
        static int EraseBlock(const lfs_config* config, lfs_block_t block);
        static int SyncDevice(const lfs_config* config);

        void Complete(const std::function<void(fs::Result result)>& onDone, int error);
        int List(std::string_view path, std::vector<DirectoryEntry>& entries);
        lfs_file_t& File(FileHandle file) const;

    private:
        const LittleFsConfiguration& configuration;
        SynchronousFlash device;
        std::vector<uint8_t> readBuffer;
        std::vector<uint8_t> programBuffer;
        std::vector<uint8_t> lookaheadBuffer;
        lfs_config config{};
        mutable lfs_t lfs{};
        mutable std::array<OpenFile, maxOpenFiles> files;
        CompletionQueue completions;
    };
}

#endif
