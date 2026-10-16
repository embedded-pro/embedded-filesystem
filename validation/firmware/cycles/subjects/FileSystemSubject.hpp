#ifndef VALIDATION_FIRMWARE_CYCLES_SUBJECTS_FILE_SYSTEM_SUBJECT_HPP
#define VALIDATION_FIRMWARE_CYCLES_SUBJECTS_FILE_SYSTEM_SUBJECT_HPP

#include "fs/fat/FatFormatter.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/cycles/CycleSubject.hpp"
#include "validation/firmware/cycles/subjects/CycleConfigurations.hpp"

namespace validation
{
    class FileSystemSubject
        : public CycleSubject
    {
    public:
        static constexpr int32_t shortTransfer = 100;

        infra::BoundedConstString Name() const override;
        std::size_t RamBytes() const override;
        bool Idle() const override;

        void Reset(const Done& onDone) override;
        void Format(const Done& onDone) override;
        void Mount(const Done& onDone) override;
        void Unmount(const Done& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const Done& onDone) override;
        void Open(infra::BoundedConstString path, CycleOpenMode mode, const Done& onDone) override;
        void Seek(uint32_t position) override;
        void Read(infra::ByteRange buffer, const Done& onDone) override;
        void Write(infra::ConstByteRange data, const Done& onDone) override;
        void Sync(const Done& onDone) override;
        void Close(const Done& onDone) override;
        void List(infra::BoundedConstString path, const Done& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone) override;
        void Remove(infra::BoundedConstString path, const Done& onDone) override;

    protected:
        FileSystemSubject(infra::BoundedConstString name, fs::FileSystem& fileSystem, std::size_t ramBytes);
        FileSystemSubject(const FileSystemSubject& other) = delete;
        FileSystemSubject& operator=(const FileSystemSubject& other) = delete;
        ~FileSystemSubject() = default;

        virtual void StartFormat(const infra::Function<void(fs::Result result)>& onDone) = 0;

    private:
        infra::Function<void(fs::Result result)> Completion();
        void Complete(int32_t status);
        void ResetUnmount();
        void NextEntry();

    private:
        infra::BoundedConstString name;
        fs::FileSystem& fileSystem;
        std::size_t ramBytes;
        infra::AutoResetFunction<void(int32_t status)> onDone;
        infra::SharedPtr<fs::File> file;
        infra::SharedPtr<fs::Directory> directory;
        std::size_t expectedBytes{ 0 };
    };

    namespace detail
    {
        // A base before FileSystemSubject, so that the volume outlives the handles that FileSystemSubject holds
        struct EmfsFatVolume
        {
            explicit EmfsFatVolume(hal::BlockDevice& device);

            cycles::FatFileSystem volume;
            fs::FatFormatter::WithStorage<8> formatter;
        };

        struct EmfsNorVolume
        {
            explicit EmfsNorVolume(hal::Flash& flash);

            cycles::NorFileSystem volume;
        };
    }

    class EmfsFatSubject
        : private detail::EmfsFatVolume
        , public FileSystemSubject
    {
    public:
        explicit EmfsFatSubject(hal::BlockDevice& device);

    private:
        void StartFormat(const infra::Function<void(fs::Result result)>& onDone) override;
    };

    class EmfsNorSubject
        : private detail::EmfsNorVolume
        , public FileSystemSubject
    {
    public:
        explicit EmfsNorSubject(hal::Flash& flash);

    private:
        void StartFormat(const infra::Function<void(fs::Result result)>& onDone) override;
    };
}

#endif
