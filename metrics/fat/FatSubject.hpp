#ifndef METRICS_FAT_FAT_SUBJECT_HPP
#define METRICS_FAT_FAT_SUBJECT_HPP

#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/device/VirtualTime.hpp"
#include "metrics/workload/FileSystemUnderTest.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace metrics
{
    class FatSubject
        : public FileSystemUnderTest
    {
    public:
        using FileSystem = fs::FatFileSystem::WithStorage<2, 8>;

        FatSubject(hal::BlockDevice& device, const BlockDeviceModel& model);

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
        struct Call
        {
            std::string path;
            std::function<void(fs::Result result)> onDone;
            std::function<void(fs::Result result, FileHandle file)> onOpen;
            std::function<void(fs::Result result, std::size_t bytesRead)> onRead;
            std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)> onList;
            std::function<void(fs::Result result, VolumeUsage usage)> onUsage;
            std::vector<DirectoryEntry> entries;
            infra::SharedPtr<fs::Directory> directory;
            FileHandle handle{ 0 };
        };

        Call& Start(std::string_view path = {});
        infra::BoundedConstString PathOf(const Call& call) const;
        infra::Function<void(fs::Result result)> Finisher(Call& call);
        void Finish(Call& call, fs::Result result);
        void Opened(Call& call, fs::Result result, infra::SharedPtr<fs::File> file);
        void NextEntry(Call& call);
        void Listed(Call& call, fs::Result result);
        void Erase(const Call& call);
        fs::File& File(FileHandle file) const;

    private:
        hal::BlockDevice& device;
        const BlockDeviceModel& model;
        FileSystem fileSystem;
        std::optional<fs::FatFormatter::WithStorage<8>> formatter;
        std::array<infra::SharedPtr<fs::File>, 2> files;
        std::list<Call> calls;
    };
}

#endif
