#ifndef METRICS_NOR_NOR_SUBJECT_HPP
#define METRICS_NOR_NOR_SUBJECT_HPP

#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include "hal/interfaces/Flash.hpp"
#include "metrics/workload/FileSystemUnderTest.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace metrics
{
    class NorSubject
        : public FileSystemUnderTest
    {
    public:
        template<fs::NorConfig Config>
        class WithConfig;

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

        std::optional<VolumeCounters> Counters() const override;

    protected:
        NorSubject(fs::NorFileSystem& fileSystem, std::size_t ramBytes, uint32_t sectorsPerBlock);

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
        fs::NorFileSystem& fileSystem;
        std::size_t ramBytes;
        uint32_t sectorsPerBlock;
        VolumeCounters earlierMounts;
        std::array<infra::SharedPtr<fs::File>, 2> files;
        std::list<Call> calls;
    };

    namespace detail
    {
        template<fs::NorConfig Config>
        struct NorSubjectStorage
        {
            explicit NorSubjectStorage(hal::Flash& flash)
                : storedFileSystem(flash)
            {}

            fs::NorFileSystem::WithStorage<Config, 2> storedFileSystem;
        };
    }

    template<fs::NorConfig Config>
    class NorSubject::WithConfig
        : private detail::NorSubjectStorage<Config>
        , public NorSubject
    {
    public:
        explicit WithConfig(hal::Flash& flash)
            : detail::NorSubjectStorage<Config>(flash)
            , NorSubject(this->storedFileSystem, sizeof(fs::NorFileSystem::WithStorage<Config, 1>), Config.sectorsPerBlock)
        {}
    };
}

#endif
