#include "validation/firmware/cycles/subjects/FileSystemSubject.hpp"

namespace validation
{
    namespace
    {
        fs::OpenMode ToOpenMode(CycleOpenMode mode)
        {
            switch (mode)
            {
                case CycleOpenMode::read:
                    return fs::OpenMode::read;
                case CycleOpenMode::write:
                    return fs::OpenMode::write;
                case CycleOpenMode::append:
                    return fs::OpenMode::append;
                default:
                    return fs::OpenMode::readWrite;
            }
        }
    }

    FileSystemSubject::FileSystemSubject(infra::BoundedConstString name, fs::FileSystem& fileSystem, std::size_t ramBytes)
        : name(name)
        , fileSystem(fileSystem)
        , ramBytes(ramBytes)
    {}

    infra::BoundedConstString FileSystemSubject::Name() const
    {
        return name;
    }

    std::size_t FileSystemSubject::RamBytes() const
    {
        return ramBytes;
    }

    bool FileSystemSubject::Idle() const
    {
        return fileSystem.Idle();
    }

    void FileSystemSubject::Reset(const Done& onDone)
    {
        this->onDone = onDone;
        directory = nullptr;

        if (file == nullptr)
            ResetUnmount();
        else
            file->Close([this](fs::Result)
                {
                    file = nullptr;
                    ResetUnmount();
                });
    }

    void FileSystemSubject::Format(const Done& onDone)
    {
        this->onDone = onDone;
        StartFormat(Completion());
    }

    void FileSystemSubject::Mount(const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.Mount(Completion());
    }

    void FileSystemSubject::Unmount(const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.Unmount(Completion());
    }

    void FileSystemSubject::MakeDirectory(infra::BoundedConstString path, const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.MakeDirectory(path, Completion());
    }

    void FileSystemSubject::Open(infra::BoundedConstString path, CycleOpenMode mode, const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.Open(path, ToOpenMode(mode), [this](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                file = opened;
                Complete(static_cast<int32_t>(result));
            });
    }

    void FileSystemSubject::Seek(uint32_t position)
    {
        file->Seek(position);
    }

    void FileSystemSubject::Read(infra::ByteRange buffer, const Done& onDone)
    {
        this->onDone = onDone;
        expectedBytes = buffer.size();
        file->Read(buffer, [this](fs::Result result, std::size_t bytesRead)
            {
                Complete(result == fs::Result::success && bytesRead != expectedBytes ? shortTransfer : static_cast<int32_t>(result));
            });
    }

    void FileSystemSubject::Write(infra::ConstByteRange data, const Done& onDone)
    {
        this->onDone = onDone;
        file->Write(data, Completion());
    }

    void FileSystemSubject::Sync(const Done& onDone)
    {
        this->onDone = onDone;
        file->Sync(Completion());
    }

    void FileSystemSubject::Close(const Done& onDone)
    {
        this->onDone = onDone;
        file->Close([this](fs::Result result)
            {
                file = nullptr;
                Complete(static_cast<int32_t>(result));
            });
    }

    void FileSystemSubject::List(infra::BoundedConstString path, const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.OpenDirectory(path, [this](fs::Result result, infra::SharedPtr<fs::Directory> opened)
            {
                if (result != fs::Result::success)
                    Complete(static_cast<int32_t>(result));
                else
                {
                    directory = opened;
                    NextEntry();
                }
            });
    }

    void FileSystemSubject::Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.Rename(from, to, Completion());
    }

    void FileSystemSubject::Remove(infra::BoundedConstString path, const Done& onDone)
    {
        this->onDone = onDone;
        fileSystem.Remove(path, Completion());
    }

    infra::Function<void(fs::Result result)> FileSystemSubject::Completion()
    {
        return [this](fs::Result result)
        {
            Complete(static_cast<int32_t>(result));
        };
    }

    void FileSystemSubject::Complete(int32_t status)
    {
        onDone(status);
    }

    void FileSystemSubject::ResetUnmount()
    {
        fileSystem.Unmount([this](fs::Result)
            {
                Complete(0);
            });
    }

    void FileSystemSubject::NextEntry()
    {
        directory->Next([this](fs::Result result, std::optional<fs::EntryInfo> entry)
            {
                if (result == fs::Result::success && entry)
                    NextEntry();
                else
                {
                    directory = nullptr;
                    Complete(static_cast<int32_t>(result));
                }
            });
    }

    detail::EmfsFatVolume::EmfsFatVolume(hal::BlockDevice& device)
        : volume(device)
        , formatter(device)
    {}

    detail::EmfsFatVolume::~EmfsFatVolume() = default;

    detail::EmfsNorVolume::EmfsNorVolume(hal::Flash& flash)
        : volume(flash)
    {}

    detail::EmfsNorVolume::~EmfsNorVolume() = default;

    EmfsFatSubject::EmfsFatSubject(hal::BlockDevice& device)
        : detail::EmfsFatVolume(device)
        , FileSystemSubject("embedded-filesystem/fat", volume, cycles::fatRamBytes)
    {}

    void EmfsFatSubject::StartFormat(const infra::Function<void(fs::Result result)>& onDone)
    {
        formatter.Format(fs::FatFormatConfig{}, onDone);
    }

    EmfsNorSubject::EmfsNorSubject(hal::Flash& flash)
        : detail::EmfsNorVolume(flash)
        , FileSystemSubject("embedded-filesystem/nor", volume, cycles::norRamBytes)
    {}

    void EmfsNorSubject::StartFormat(const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Format(onDone);
    }
}
