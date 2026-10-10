#include "metrics/baselines/fatfs/FatFsSubject.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "metrics/baselines/fatfs/DiskIo.hpp"
#include <algorithm>
#include <string>

namespace metrics
{
    namespace
    {
        constexpr std::size_t formatWorkAreaSize = 32 * 1024;

        BYTE Flags(OpenMode mode)
        {
            switch (mode)
            {
                case OpenMode::read:
                    return FA_READ | FA_OPEN_EXISTING;
                case OpenMode::write:
                    return FA_WRITE | FA_CREATE_ALWAYS;
                case OpenMode::append:
                    return FA_WRITE | FA_OPEN_APPEND;
                default:
                    return FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
            }
        }
    }

    fs::Result FatFsResult(FRESULT result)
    {
        switch (result)
        {
            case FR_OK:
                return fs::Result::success;
            case FR_NO_FILE:
            case FR_NO_PATH:
                return fs::Result::notFound;
            case FR_INVALID_NAME:
            case FR_INVALID_OBJECT:
            case FR_INVALID_DRIVE:
            case FR_INVALID_PARAMETER:
                return fs::Result::invalidPath;
            case FR_DENIED:
                return fs::Result::noSpace;
            case FR_EXIST:
                return fs::Result::alreadyExists;
            case FR_WRITE_PROTECTED:
                return fs::Result::readOnly;
            case FR_NOT_ENABLED:
                return fs::Result::notMounted;
            case FR_INT_ERR:
            case FR_NO_FILESYSTEM:
                return fs::Result::corrupted;
            case FR_NOT_READY:
                return fs::Result::mediaRemoved;
            case FR_TIMEOUT:
            case FR_LOCKED:
                return fs::Result::busy;
            case FR_NOT_ENOUGH_CORE:
            case FR_TOO_MANY_OPEN_FILES:
                return fs::Result::tooManyOpenFiles;
            default:
                return fs::Result::deviceError;
        }
    }

    FatFsSubject::FatFsSubject(VirtualTime& time, hal::BlockDevice& device, uint32_t allocationUnitBlocks)
        : device(time, device)
        , allocationUnitBlocks(allocationUnitBlocks)
    {
        really_assert(DiskIo().device == nullptr);
        DiskIo() = DiskIoBinding{ &this->device, tuned ? allocationUnitBlocks : 0 };
    }

    FatFsSubject::~FatFsSubject()
    {
        DiskIo() = DiskIoBinding{};
    }

    std::string_view FatFsSubject::Name() const
    {
        return EMFS_FATFS_NAME;
    }

    std::size_t FatFsSubject::RamBytes() const
    {
        std::size_t linkMap = tuned ? sizeof(OpenFile::linkMap) : 0;
        return sizeof(FATFS) + sizeof(FIL) + linkMap + (FF_MAX_LFN + 1) * sizeof(WCHAR);
    }

    void FatFsSubject::Format(const std::function<void(fs::Result result)>& onDone)
    {
        std::vector<uint8_t> workArea(formatWorkAreaSize);
        MKFS_PARM parameters{ FM_ANY, 0, allocationUnitBlocks, 0, 0 };
        Complete(onDone, f_mkfs("", tuned ? &parameters : nullptr, workArea.data(), static_cast<UINT>(workArea.size())));
    }

    void FatFsSubject::Mount(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, f_mount(&volume, "", 1));
    }

    void FatFsSubject::Unmount(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, f_unmount(""));
    }

    void FatFsSubject::Open(std::string_view path, OpenMode mode, const std::function<void(fs::Result result, FileHandle file)>& onDone)
    {
        auto slot = std::find_if(files.begin(), files.end(), [](const OpenFile& file)
            {
                return !file.open;
            });
        auto handle = static_cast<FileHandle>(slot - files.begin());
        auto result = slot == files.end() ? FR_TOO_MANY_OPEN_FILES : Open(*slot, path, mode);

        completions.Defer([onDone, result, handle]
            {
                onDone(FatFsResult(result), handle);
            });
    }

    void FatFsSubject::Read(FileHandle file, infra::ByteRange buffer, const std::function<void(fs::Result result, std::size_t bytesRead)>& onDone)
    {
        UINT read = 0;
        auto result = f_read(&File(file), buffer.begin(), static_cast<UINT>(buffer.size()), &read);

        completions.Defer([onDone, result, read]
            {
                onDone(FatFsResult(result), read);
            });
    }

    void FatFsSubject::Write(FileHandle file, infra::ConstByteRange data, const std::function<void(fs::Result result)>& onDone)
    {
        UINT written = 0;
        auto result = f_write(&File(file), data.begin(), static_cast<UINT>(data.size()), &written);

        completions.Defer([onDone, result, complete = written == data.size()]
            {
                onDone(result == FR_OK && !complete ? fs::Result::noSpace : FatFsResult(result));
            });
    }

    void FatFsSubject::Seek(FileHandle file, uint32_t position)
    {
        f_lseek(&File(file), position);
    }

    uint32_t FatFsSubject::Size(FileHandle file) const
    {
        return static_cast<uint32_t>(f_size(&File(file)));
    }

    void FatFsSubject::Sync(FileHandle file, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, f_sync(&File(file)));
    }

    void FatFsSubject::Close(FileHandle file, const std::function<void(fs::Result result)>& onDone)
    {
        auto result = f_close(&File(file));
        files[file].open = false;
        Complete(onDone, result);
    }

    void FatFsSubject::ExpectSize([[maybe_unused]] FileHandle file, [[maybe_unused]] uint32_t bytes, const std::function<void(fs::Result result)>& onDone)
    {
#if FF_USE_EXPAND
        Complete(onDone, f_expand(&File(file), bytes, 1));
#else
        completions.Defer([onDone]
            {
                onDone(fs::Result::unsupported);
            });
#endif
    }

    void FatFsSubject::Remove(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, f_unlink(std::string{ path }.c_str()));
    }

    void FatFsSubject::MakeDirectory(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, f_mkdir(std::string{ path }.c_str()));
    }

    void FatFsSubject::List(std::string_view path, const std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)>& onDone)
    {
        std::vector<DirectoryEntry> entries;
        auto result = List(path, entries);

        completions.Defer([onDone, result, entries]
            {
                onDone(FatFsResult(result), entries);
            });
    }

    void FatFsSubject::QueryUsage(const std::function<void(fs::Result result, VolumeUsage usage)>& onDone)
    {
        DWORD freeClusters = 0;
        FATFS* mounted = nullptr;
        auto result = f_getfree("", &freeClusters, &mounted);

        VolumeUsage usage{};
        if (result == FR_OK)
        {
            uint64_t clusterSize = static_cast<uint64_t>(mounted->csize) * device.BlockSize();
            usage.totalBytes = (mounted->n_fatent - 2) * clusterSize;
            usage.usedBytes = usage.totalBytes - freeClusters * clusterSize;
        }

        completions.Defer([onDone, result, usage]
            {
                onDone(FatFsResult(result), usage);
            });
    }

    void FatFsSubject::IdleHint(const std::function<void()>& onDone)
    {
        completions.Defer(onDone);
    }

    void FatFsSubject::Complete(const std::function<void(fs::Result result)>& onDone, FRESULT result)
    {
        completions.Defer([onDone, result]
            {
                onDone(FatFsResult(result));
            });
    }

    FRESULT FatFsSubject::Open(OpenFile& slot, std::string_view path, OpenMode mode)
    {
        auto result = f_open(&slot.file, std::string{ path }.c_str(), Flags(mode));
        if (result != FR_OK)
            return result;

        slot.open = true;
        if (mode == OpenMode::read)
            CreateLinkMap(slot);

        return FR_OK;
    }

    void FatFsSubject::CreateLinkMap(OpenFile& slot)
    {
#if FF_USE_FASTSEEK
        slot.linkMap[0] = linkMapEntries;
        slot.file.cltbl = slot.linkMap.data();
        if (f_lseek(&slot.file, CREATE_LINKMAP) != FR_OK)
            slot.file.cltbl = nullptr;
#endif
    }

    FRESULT FatFsSubject::List(std::string_view path, std::vector<DirectoryEntry>& entries)
    {
        DIR directory{};
        auto result = f_opendir(&directory, std::string{ path }.c_str());
        if (result != FR_OK)
            return result;

        FILINFO information{};
        for (result = f_readdir(&directory, &information); result == FR_OK && information.fname[0] != 0; result = f_readdir(&directory, &information))
            entries.push_back({ information.fname, (information.fattrib & AM_DIR) != 0, static_cast<uint32_t>(information.fsize) });

        f_closedir(&directory);
        return result;
    }

    FIL& FatFsSubject::File(FileHandle file) const
    {
        really_assert(file < files.size() && files[file].open);
        return files[file].file;
    }
}
